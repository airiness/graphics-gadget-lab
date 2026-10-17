#include "Application/Lab/Sessions/RenderGraphComputeLabSession.h"
#include "GGLabRuntime/Graphics/RenderParameters.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureAccess.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchDefaults.h"
#include "GGLabRuntime/Diagnostics/Snapshots/LabSnapshot.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineOverlayExtensionBase.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "ShaderArtifactRuntime/NonFiniteCounterABI.h"

#include <array>
#include <cstddef>
#include <optional>

namespace gglab
{
	struct RenderGraphComputeLabState
	{
		std::atomic<uint64_t> m_InitializeExecutions = 0;
		std::atomic<uint64_t> m_WriteExecutions = 0;
		std::atomic<uint64_t> m_ReadWriteExecutions = 0;
		std::atomic<uint64_t> m_ProbeExecutions = 0;
		std::atomic<uint64_t> m_PreviewExecutions = 0;
		std::atomic<uint64_t> m_CulledExecutions = 0;
		std::atomic<float> m_AnimationTimeSeconds = 0.0f;
		std::atomic<float> m_AnimationSpeed = 1.0f;
		std::atomic<float> m_PatternFrequency = 18.0f;
		std::atomic<float> m_RingRadius = 0.46f;
		std::atomic<float> m_RingIntensity = 0.82f;
		std::atomic<uint32_t> m_CheckerCellSize = 32;
	};

	namespace
	{
		constexpr uint32_t WorkTextureWidth = 512;
		constexpr uint32_t WorkTextureHeight = 512;
		constexpr uint32_t ComputeThreadGroupSize = 8;

		const LabParameterId AnimationSpeedId("render_graph_compute.animation.speed");
		const LabParameterId PatternFrequencyId("render_graph_compute.pattern.frequency");
		const LabParameterId RingRadiusId("render_graph_compute.ring.radius");
		const LabParameterId RingIntensityId("render_graph_compute.ring.intensity");
		const LabParameterId CheckerCellSizeId("render_graph_compute.checker.cell_size");

		// The watch's boundary injection point in CSNonFiniteProbe; the probe exists
		// only for frames whose production counters request that point.
		[[nodiscard]] bool RequestsNonFiniteProbe(const RenderServices& services) noexcept
		{
			const std::optional<NonFiniteInjection> injection =
				services.m_FrameCapture ? services.m_FrameCapture->GetNonFiniteInjection() : std::nullopt;
			return injection && injection->m_PointIndex < nonfinite::InjectionPoints.size() &&
				nonfinite::InjectionPoints[injection->m_PointIndex].m_Id ==
				"pass-render-graph-compute-smoke.csnonfiniteprobe.probe";
		}

		const RenderPassInfo NonFiniteProbePassInfo{
			.m_TypeName = "Lab.RenderGraphCompute.NonFiniteProbe",
			.m_DisplayName = "RenderGraph Compute Non-finite Probe",
			.m_CategoryName = "Debug",
			.m_Description = "Writes a float probe at the watch's boundary injection point.",
			.m_Category = RenderPassCategory::Debug,
			.m_Type = RenderPassType::Compute,
		};

		const RenderPassInfo ComputeWritePassInfo{
			.m_TypeName = "Lab.RenderGraphCompute.Write",
			.m_DisplayName = "RenderGraph Compute Write",
			.m_CategoryName = "Debug",
			.m_Description = "Writes two transient textures through direct-list compute.",
			.m_Category = RenderPassCategory::Debug,
			.m_Type = RenderPassType::Compute,
		};

		const RenderPassInfo ComputeReadWritePassInfo{
			.m_TypeName = "Lab.RenderGraphCompute.ReadWrite",
			.m_DisplayName = "RenderGraph Compute Read/Write",
			.m_CategoryName = "Debug",
			.m_Description = "Updates one texture after an ordered UAV dependency.",
			.m_Category = RenderPassCategory::Debug,
			.m_Type = RenderPassType::Compute,
		};

		const RenderPassInfo PreviewPassInfo{
			.m_TypeName = "Lab.RenderGraphCompute.Preview",
			.m_DisplayName = "RenderGraph Compute Preview",
			.m_CategoryName = "Debug",
			.m_Description = "Previews both compute textures on the swap-chain back buffer.",
			.m_Category = RenderPassCategory::Debug,
			.m_Type = RenderPassType::Graphics,
		};

		struct ComputeLabResources
		{
			RGTextureId m_WorkA{};
			RGTextureId m_WorkB{};
			// Present only while the watch injects at the probe's boundary point.
			RGTextureId m_NonFiniteProbe{};
			RGTextureId m_BackBuffer{};
		};

		const char* ComputeLabResourcesName = "RenderGraphComputeLab.Resources";

		struct SetupPassData
		{
		};

		struct InitializePassData
		{
			RGTextureViewId m_WorkRtv{};
		};

		struct ComputeWritePassData
		{
			RGTextureViewId m_WorkAUav{};
			RGTextureViewId m_WorkBUav{};
		};

		struct ComputeReadWritePassData
		{
			RGTextureViewId m_WorkAUav{};
		};

		struct NonFiniteProbePassData
		{
			RGTextureViewId m_ProbeUav{};
		};

		struct CulledComputePassData
		{
			RGTextureViewId m_OutputUav{};
		};

		struct PreviewPassData
		{
			RGTextureViewId m_WorkASrv{};
			RGTextureViewId m_WorkBSrv{};
			RGTextureViewId m_BackBufferRtv{};
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};

		struct FinishPassData
		{
		};

		struct ComputeLabPassParameters
		{
			uint32_t m_WorkAIndex = 0;
			uint32_t m_WorkBIndex = 0;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_Padding0 = 0;
			float m_PatternFrequency = 18.0f;
			float m_RingRadius = 0.46f;
			float m_RingIntensity = 0.82f;
			uint32_t m_CheckerCellSize = 32;
			uint32_t m_ProbeIndex = 0;
			std::array<uint32_t, 2> m_Padding1{};
			std::array<float, 4> m_CheckerDarkColor{ 0.02f, 0.08f, 0.15f, 1.0f };
			std::array<float, 4> m_CheckerLightColor{ 0.18f, 0.75f, 0.92f, 1.0f };
			std::array<float, 4> m_RingColor{ 1.0f, 0.28f, 0.08f, 1.0f };
		};
		static_assert(IsPassConstantBufferStruct<ComputeLabPassParameters>);
		static_assert(sizeof(ComputeLabPassParameters) == 96);
		static_assert(offsetof(ComputeLabPassParameters, m_CheckerDarkColor) == 48);
		static_assert(offsetof(ComputeLabPassParameters, m_CheckerLightColor) == 64);
		static_assert(offsetof(ComputeLabPassParameters, m_RingColor) == 80);
		static_assert(offsetof(ComputeLabPassParameters, m_ProbeIndex) == 36);

		struct ComputeLabInlineParameters
		{
			float m_Phase = 0.0f;
			float m_PreviewSplit = 0.68f;
		};
		static_assert(IsInlineConstantStruct<ComputeLabInlineParameters>);
		static_assert(sizeof(ComputeLabInlineParameters) == 8);

		RHITextureDesc MakeWorkTextureDesc() noexcept
		{
			RHITextureDesc desc{};
			// R32_UINT typed UAV loads are guaranteed by D3D12. The previous
			// R16G16B16A16_FLOAT workload required optional typed-load support.
			desc.m_Format = RHIFormat::R32Uint;
			desc.m_Extent = { WorkTextureWidth, WorkTextureHeight, 1u };
			return desc;
		}

		RHITextureDesc MakeNonFiniteProbeDesc() noexcept
		{
			RHITextureDesc desc{};
			// Float32 storage keeps every injected bit pattern; the probe is only
			// written (typed UAV store) and read by boundary scans.
			desc.m_Format = RHIFormat::R32G32B32A32Float;
			desc.m_Extent = { WorkTextureWidth, WorkTextureHeight, 1u };
			return desc;
		}

		class RenderGraphComputeLabPipeline final : public RenderPipelineBase
		{
		public:
			explicit RenderGraphComputeLabPipeline(
				std::shared_ptr<RenderGraphComputeLabState> state) noexcept :
				m_State(std::move(state))
			{
				GGLAB_ASSERT_NOT_NULL(m_State.get());
			}

			std::string_view GetName() const noexcept override { return "RenderGraph Compute Lab"; }

			void BuildRenderGraph(RenderGraph& rg, const RenderFrameContext& context,
				const RenderServices& services) noexcept override
			{
				rg.GetBlackboard().GetOrCreate<NonFiniteWatchDefaultRegistry>().Declare({});
				GGLAB_ASSERT_MSG(context.IsValid(), "RenderFrameContext invalid.");
				GGLAB_ASSERT_MSG(services.IsValid(), "RenderServices invalid.");
				EnsureInitialized(services);

				auto* swapChain = m_Presentation->GetSwapChain();
				GGLAB_ASSERT_NOT_NULL(swapChain);

				const uint32_t backBufferIndex = context.m_BackBufferIndex;
				const uint32_t displayWidth = swapChain->GetBufferWidth();
				const uint32_t displayHeight = swapChain->GetBufferHeight();
				const RenderViewID displayViewId = context.GetDisplayViewId();
				// Every pass of the frame receives the same pattern values; each pass sets
				// only its own descriptor indices.
				const ComputeLabPassParameters frameParameters{
					.m_Width = WorkTextureWidth,
					.m_Height = WorkTextureHeight,
					.m_PatternFrequency = m_State->m_PatternFrequency.load(std::memory_order_relaxed),
					.m_RingRadius = m_State->m_RingRadius.load(std::memory_order_relaxed),
					.m_RingIntensity = m_State->m_RingIntensity.load(std::memory_order_relaxed),
					.m_CheckerCellSize = m_State->m_CheckerCellSize.load(std::memory_order_relaxed),
				};
				const ComputeLabInlineParameters inlineParameters{
					.m_Phase = std::fmod(
						m_State->m_AnimationTimeSeconds.load(std::memory_order_relaxed),
						6.28318530718f),
				};
				const bool boundaryProbe = RequestsNonFiniteProbe(services);

				rg.AddPass<SetupPassData>("Lab.RenderGraphCompute.Setup",
					[swapChain, backBufferIndex, displayWidth, displayHeight, displayViewId, boundaryProbe]
					(RenderGraph::RGBuilder& builder, SetupPassData&)
					{
						builder.SideEffect();

						auto& resources = builder.GetBlackboard().Create<ComputeLabResources>(
							ComputeLabResourcesName);
						resources.m_WorkA = builder.CreateTexture(
							"RenderGraphCompute.WorkA", MakeWorkTextureDesc());
						resources.m_WorkB = builder.CreateTexture(
							"RenderGraphCompute.WorkB", MakeWorkTextureDesc());
						if (boundaryProbe)
						{
							resources.m_NonFiniteProbe = builder.CreateTexture(
								"RenderGraphCompute.NonFiniteProbe", MakeNonFiniteProbeDesc());
						}

						RHITextureDesc backBufferDesc{};
						backBufferDesc.m_Format = swapChain->GetFormat();
						backBufferDesc.m_Extent = { displayWidth, displayHeight, 1u };
						resources.m_BackBuffer =
							builder.ImportTexture("RenderGraphCompute.BackBuffer",
								swapChain->GetBackBufferHandle(backBufferIndex), backBufferDesc,
								swapChain->GetBackBufferInitialState(backBufferIndex),
								RGContentValidity::Undefined);

						auto& targets = builder.GetBlackboard()
							.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						// The Lab composes only display-domain targets.
						targets.m_DisplayWidth = displayWidth;
						targets.m_DisplayHeight = displayHeight;
						targets.m_BackBuffer = resources.m_BackBuffer;
					});

				rg.AddPass<InitializePassData>(
					"Lab.RenderGraphCompute.Initialize",
					[](RenderGraph::RGBuilder& builder, InitializePassData& data)
					{
						auto& resources = builder.GetBlackboard().Get<ComputeLabResources>(
							ComputeLabResourcesName);
						builder.WriteInPlace(resources.m_WorkA, RGTextureAccess::RenderTarget,
							RHIStage::RenderTarget);
						data.m_WorkRtv =
							builder.CreateView<RHITextureViewType::RenderTarget>(resources.m_WorkA);
					},
					[state = m_State](RGExecuteContext& executeContext, InitializePassData& data)
					{
						auto* commandContext = executeContext.GetGraphicsCommandContext();
						const auto workRtv = executeContext.GetViewHandle(data.m_WorkRtv);
						const RHIRenderingAttachment colorAttachment{
							.m_View = workRtv,
							.m_LoadOp = RHIContentLoadOp::DontCare,
						};
						commandContext->BeginRendering({ .m_ColorAttachments =
							std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
						commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
						state->m_InitializeExecutions.fetch_add(1, std::memory_order_relaxed);
					});

				rg.AddPass<ComputeWritePassData>(
					ComputeWritePassInfo.m_TypeName.c_str(), RGPassEncoderType::Compute,
					[](RenderGraph::RGBuilder& builder, ComputeWritePassData& data)
					{
						auto& resources = builder.GetBlackboard().Get<ComputeLabResources>(
							ComputeLabResourcesName);
						builder.ReadWriteInPlace(resources.m_WorkA,
							RGTextureAccess::StorageReadWrite, RHIStage::ComputeShader);
						builder.WriteInPlace(resources.m_WorkB, RGTextureAccess::StorageWrite,
							RHIStage::ComputeShader);
						data.m_WorkAUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
							resources.m_WorkA);
						data.m_WorkBUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
							resources.m_WorkB);
					},
					[this, frameParameters, inlineParameters](
						RGExecuteContext& executeContext, ComputeWritePassData& data)
					{
						auto* commandContext = executeContext.GetDirectComputeCommandContext();
						const auto workAUav = executeContext.GetViewDescriptor(data.m_WorkAUav);
						const auto workBUav = executeContext.GetViewDescriptor(data.m_WorkBUav);
						GGLAB_ASSERT_MSG(workAUav.IsValid() && workBUav.IsValid(),
							"Compute Lab UAVs must be shader visible.");

						ComputeLabPassParameters parameters = frameParameters;
						parameters.m_WorkAIndex = workAUav.m_Index;
						parameters.m_WorkBIndex = workBUav.m_Index;
						commandContext->SetPipeline(GetOrCreateComputeWritePSO());
						commandContext->SetInlineConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::InlineConstants), inlineParameters);
						if (!commandContext->SetConstantBufferData(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
						{
							return;
						}
						DispatchWorkTexture(*commandContext);
						m_State->m_WriteExecutions.fetch_add(1, std::memory_order_relaxed);
					});

				rg.AddPass<ComputeReadWritePassData>(
					ComputeReadWritePassInfo.m_TypeName.c_str(), RGPassEncoderType::Compute,
					[](RenderGraph::RGBuilder& builder, ComputeReadWritePassData& data)
					{
						auto& resources = builder.GetBlackboard().Get<ComputeLabResources>(
							ComputeLabResourcesName);
						builder.ReadWriteInPlace(resources.m_WorkA,
							RGTextureAccess::StorageReadWrite, RHIStage::ComputeShader);
						data.m_WorkAUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
							resources.m_WorkA);
					},
					[this, frameParameters, inlineParameters](
						RGExecuteContext& executeContext, ComputeReadWritePassData& data)
					{
						auto* commandContext = executeContext.GetDirectComputeCommandContext();
						const auto workAUav = executeContext.GetViewDescriptor(data.m_WorkAUav);
						GGLAB_ASSERT_MSG(workAUav.IsValid(),
							"Compute Lab read/write UAV must be shader visible.");

						ComputeLabPassParameters parameters = frameParameters;
						parameters.m_WorkAIndex = workAUav.m_Index;
						commandContext->SetPipeline(GetOrCreateComputeReadWritePSO());
						commandContext->SetInlineConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::InlineConstants), inlineParameters);
						if (!commandContext->SetConstantBufferData(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
						{
							return;
						}
						DispatchWorkTexture(*commandContext);
						m_State->m_ReadWriteExecutions.fetch_add(1, std::memory_order_relaxed);
					});

				if (boundaryProbe)
				{
					rg.AddPass<NonFiniteProbePassData>(
						NonFiniteProbePassInfo.m_TypeName.c_str(), RGPassEncoderType::Compute,
						[](RenderGraph::RGBuilder& builder, NonFiniteProbePassData& data)
						{
							auto& resources = builder.GetBlackboard().Get<ComputeLabResources>(
								ComputeLabResourcesName);
							builder.WriteInPlace(resources.m_NonFiniteProbe,
								RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
							data.m_ProbeUav = builder.CreateView<RHITextureViewType::UnorderedAccess>(
								resources.m_NonFiniteProbe);
						},
						[this, frameParameters, inlineParameters](
							RGExecuteContext& executeContext, NonFiniteProbePassData& data)
						{
							auto* commandContext = executeContext.GetDirectComputeCommandContext();
							const auto probeUav = executeContext.GetViewDescriptor(data.m_ProbeUav);
							GGLAB_ASSERT_MSG(probeUav.IsValid(),
								"Compute Lab probe UAV must be shader visible.");

							ComputeLabPassParameters parameters = frameParameters;
							parameters.m_ProbeIndex = probeUav.m_Index;
							commandContext->SetPipeline(GetOrCreateNonFiniteProbePSO());
							commandContext->SetInlineConstants(
								static_cast<uint32_t>(CommonRSRootParamIndex::InlineConstants), inlineParameters);
							if (!commandContext->SetConstantBufferData(
								static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
							{
								return;
							}
							DispatchWorkTexture(*commandContext);
							m_State->m_ProbeExecutions.fetch_add(1, std::memory_order_relaxed);
						});
				}

				rg.AddPass<CulledComputePassData>(
					"Lab.RenderGraphCompute.Culled", RGPassEncoderType::Compute,
					[](RenderGraph::RGBuilder& builder, CulledComputePassData& data)
					{
						auto output = builder.CreateTexture(
							"RenderGraphCompute.CulledOutput", MakeWorkTextureDesc());
						builder.WriteInPlace(
							output, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
						data.m_OutputUav =
							builder.CreateView<RHITextureViewType::UnorderedAccess>(output);
					},
					[this, frameParameters, inlineParameters](
						RGExecuteContext& executeContext, CulledComputePassData& data)
					{
						auto* commandContext = executeContext.GetDirectComputeCommandContext();
						const auto outputUav = executeContext.GetViewDescriptor(data.m_OutputUav);
						ComputeLabPassParameters parameters = frameParameters;
						parameters.m_WorkAIndex = outputUav.m_Index;
						parameters.m_WorkBIndex = outputUav.m_Index;
						commandContext->SetPipeline(GetOrCreateComputeWritePSO());
						commandContext->SetInlineConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::InlineConstants), inlineParameters);
						if (!commandContext->SetConstantBufferData(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
						{
							return;
						}
						DispatchWorkTexture(*commandContext);
						m_State->m_CulledExecutions.fetch_add(1, std::memory_order_relaxed);
					});

				rg.AddPass<PreviewPassData>(
					PreviewPassInfo.m_TypeName.c_str(),
					[displayWidth, displayHeight, displayViewId](
						RenderGraph::RGBuilder& builder, PreviewPassData& data)
					{
						auto& resources = builder.GetBlackboard().Get<ComputeLabResources>(
							ComputeLabResourcesName);
						auto& targets = builder.GetBlackboard()
							.Get<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						data.m_WorkASrv =
							builder.CreateView<RHITextureViewType::ShaderResource>(builder.Read(
								resources.m_WorkA, RGTextureAccess::Sample, RHIStage::PixelShader));
						data.m_WorkBSrv =
							builder.CreateView<RHITextureViewType::ShaderResource>(builder.Read(
								resources.m_WorkB, RGTextureAccess::Sample, RHIStage::PixelShader));
						if (resources.m_NonFiniteProbe.IsValid())
						{
							// The declared read keeps the probe live for boundary scans; the
							// preview does not display it.
							GGLAB_UNUSED(builder.Read(resources.m_NonFiniteProbe,
								RGTextureAccess::Sample, RHIStage::PixelShader));
						}
						builder.WriteInPlace(targets.m_BackBuffer, RGTextureAccess::RenderTarget,
							RHIStage::RenderTarget);
						resources.m_BackBuffer = targets.m_BackBuffer;
						data.m_BackBufferRtv = builder.CreateView<RHITextureViewType::RenderTarget>(
							targets.m_BackBuffer);
						data.m_Width = displayWidth;
						data.m_Height = displayHeight;
					},
					[this, frameParameters, inlineParameters]
					(RGExecuteContext& executeContext, PreviewPassData& data)
					{
						auto* commandContext = executeContext.GetGraphicsCommandContext();
						const auto workASrv = executeContext.GetViewDescriptor(data.m_WorkASrv);
						const auto workBSrv = executeContext.GetViewDescriptor(data.m_WorkBSrv);
						const auto backBufferRtv =
							executeContext.GetViewHandle(data.m_BackBufferRtv);
						GGLAB_ASSERT_MSG(workASrv.IsValid() && workBSrv.IsValid(),
							"Compute Lab preview SRVs must be shader visible.");

						const RHIRenderingAttachment colorAttachment{
							.m_View = backBufferRtv,
							.m_LoadOp = RHIContentLoadOp::DontCare,
						};
						commandContext->BeginRendering({ .m_ColorAttachments =
							std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
						commandContext->ClearColorAttachment(
							0, { 0.005f, 0.008f, 0.015f, 1.0f });
						commandContext->SetPipeline(GetOrCreatePreviewPSO());
						commandContext->SetInlineConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::InlineConstants), inlineParameters);
						commandContext->SetViewport({
							0.0f,
							0.0f,
							static_cast<float>(data.m_Width),
							static_cast<float>(data.m_Height),
							});
						commandContext->SetScissorRect({
							0,
							0,
							static_cast<int32_t>(data.m_Width),
							static_cast<int32_t>(data.m_Height),
							});
						ComputeLabPassParameters parameters = frameParameters;
						parameters.m_WorkAIndex = workASrv.m_Index;
						parameters.m_WorkBIndex = workBSrv.m_Index;
						if (!commandContext->SetConstantBufferData(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
						{
							return;
						}
						commandContext->DrawFullscreenTriangle();
						m_State->m_PreviewExecutions.fetch_add(1, std::memory_order_relaxed);
					});

				// Scene captures exclude tooling overlays; composited captures include them.
				AddFrameCaptureTapPass(rg, context, services, FrameCaptureSource::Scene);
				if (services.m_OverlayExtension)
				{
					services.m_OverlayExtension->AddOverlayPasses(rg, context, services);
				}
				AddFrameCaptureTapPass(rg, context, services, FrameCaptureSource::Composited);

				rg.AddPass<FinishPassData>("Lab.RenderGraphCompute.Finish",
					[displayViewId](RenderGraph::RGBuilder& builder, FinishPassData&)
					{
						builder.SideEffect();
						auto& targets = builder.GetBlackboard()
							.Get<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						builder.Export(
							targets.m_BackBuffer, RGTextureAccess::Present, RHIStage::Present);
					});
			}

		private:
			template <typename CommandContext>
			static void DispatchWorkTexture(CommandContext& commandContext) noexcept
			{
				commandContext.Dispatch(
					(WorkTextureWidth + ComputeThreadGroupSize - 1) / ComputeThreadGroupSize,
					(WorkTextureHeight + ComputeThreadGroupSize - 1) / ComputeThreadGroupSize,
					1);
			}

			void EnsureInitialized(const RenderServices& services) noexcept
			{
				if (m_IsInitialized)
				{
					return;
				}

				auto* shaderManager = services.m_ShaderPrograms;
				GGLAB_ASSERT_NOT_NULL(shaderManager);
				m_PipelineResolver = services.m_PipelineResolver;
				m_Presentation = services.m_Presentation;
				m_BindingLayout = services.m_BindingLayout;

				m_ComputeWriteRecipe.m_CSId =
					shaderManager->LoadProgram(shader_programs::RenderGraphComputeWrite);
				m_ComputeReadWriteRecipe.m_CSId =
					shaderManager->LoadProgram(shader_programs::RenderGraphComputeReadWrite);
				m_PreviewRecipe.m_VSId = shaderManager->LoadProgram(
					shader_programs::RenderGraphComputePreviewVertex);
				m_PreviewRecipe.m_PSId = shaderManager->LoadProgram(
					shader_programs::RenderGraphComputePreviewPixel);

#if GGLAB_DEVELOPMENT_BUILD
				m_NonFiniteProbeRecipe.m_CSId =
					shaderManager->LoadProgram(shader_programs::RenderGraphComputeNonFiniteProbe);
#endif

				const auto bindingLayout = m_BindingLayout->GetCommonBindingLayout();
				m_ComputeWriteRecipe.m_BindingLayout = bindingLayout;
				m_ComputeReadWriteRecipe.m_BindingLayout = bindingLayout;
				m_NonFiniteProbeRecipe.m_BindingLayout = bindingLayout;

				m_PreviewRecipe.m_BindingLayout = bindingLayout;
				m_PreviewRecipe.m_InputLayoutId = InputLayoutID::None;
				m_PreviewRecipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
				m_PreviewRecipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
				m_PreviewRecipe.m_Formats.m_RenderTargetFormats[0] =
					m_Presentation->GetSwapChain()->GetFormat();
				m_PreviewRecipe.m_Formats.m_RenderTargetCount = 1;
				m_PreviewRecipe.m_Formats.m_DepthStencilFormat = RHIFormat::Unknown;
				m_PreviewRecipe.m_Formats.m_SampleCount = 1;
				m_PreviewRecipe.m_RasterizerPreset = RasterizerPreset::Default;
				m_PreviewRecipe.m_BlendPreset = BlendPreset::Default;
				m_PreviewRecipe.m_DepthPreset = DepthPreset::DepthDisabled;
				m_IsInitialized = true;
			}

			RHIPipelineHandle GetOrCreateComputeWritePSO() noexcept
			{
				auto* pipelineCache = m_PipelineResolver;
				GGLAB_ASSERT_NOT_NULL(pipelineCache);
				return pipelineCache->Resolve(
					m_ComputeWriteSlot, m_ComputeWriteRecipe, ComputeWritePassInfo);
			}

			RHIPipelineHandle GetOrCreateComputeReadWritePSO() noexcept
			{
				auto* pipelineCache = m_PipelineResolver;
				GGLAB_ASSERT_NOT_NULL(pipelineCache);
				return pipelineCache->Resolve(
					m_ComputeReadWriteSlot, m_ComputeReadWriteRecipe, ComputeReadWritePassInfo);
			}

			RHIPipelineHandle GetOrCreateNonFiniteProbePSO() noexcept
			{
				auto* pipelineCache = m_PipelineResolver;
				GGLAB_ASSERT_NOT_NULL(pipelineCache);
				GGLAB_ASSERT_MSG(m_NonFiniteProbeRecipe.m_CSId.IsValid(),
					"The non-finite probe exists only in Debug and Develop builds.");
				return pipelineCache->Resolve(
					m_NonFiniteProbeSlot, m_NonFiniteProbeRecipe, NonFiniteProbePassInfo);
			}

			RHIPipelineHandle GetOrCreatePreviewPSO() noexcept
			{
				auto* pipelineCache = m_PipelineResolver;
				GGLAB_ASSERT_NOT_NULL(pipelineCache);
				return pipelineCache->Resolve(m_PreviewSlot, m_PreviewRecipe, PreviewPassInfo);
			}

			std::shared_ptr<RenderGraphComputeLabState> m_State;
			RenderPipelineResolver* m_PipelineResolver = nullptr;
			RenderPresentationAccess* m_Presentation = nullptr;
			RenderBindingLayoutAccess* m_BindingLayout = nullptr;
			ComputePipelineRecipe m_ComputeWriteRecipe{};
			ComputePipelineRecipe m_ComputeReadWriteRecipe{};
			ComputePipelineRecipe m_NonFiniteProbeRecipe{};
			GraphicsPhysicalPipelineKey m_PreviewRecipe{};
			ComputePipelineSlot m_ComputeWriteSlot{};
			ComputePipelineSlot m_ComputeReadWriteSlot{};
			ComputePipelineSlot m_NonFiniteProbeSlot{};
			GraphicsPipelineSlot m_PreviewSlot{};
			bool m_IsInitialized = false;
		};
	}

	RenderGraphComputeLabSession::RenderGraphComputeLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, nullptr),
		m_State(std::make_shared<RenderGraphComputeLabState>())
	{
		SetRenderPipeline(std::make_unique<RenderGraphComputeLabPipeline>(m_State));

		auto& parameters = GetMutableParameters();
		GGLAB_UNUSED(parameters.Add({
			.m_Id = AnimationSpeedId,
			.m_Name = "Animation Speed",
			.m_Group = "Pattern",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 1.0f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(4.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = PatternFrequencyId,
			.m_Name = "Wave Frequency",
			.m_Group = "Pattern",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 18.0f,
			.m_MinValue = LabValue(2.0f),
			.m_MaxValue = LabValue(40.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = RingRadiusId,
			.m_Name = "Ring Radius",
			.m_Group = "Ring",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.46f,
			.m_MinValue = LabValue(0.1f),
			.m_MaxValue = LabValue(0.85f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = RingIntensityId,
			.m_Name = "Ring Intensity",
			.m_Group = "Ring",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.82f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(1.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = CheckerCellSizeId,
			.m_Name = "Checker Cell Size",
			.m_Group = "Checker",
			.m_Type = LabParameterType::UInt,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = uint32_t(32),
			.m_MinValue = LabValue(uint32_t(4)),
			.m_MaxValue = LabValue(uint32_t(128)),
			}));
		ApplyImmediateParameters();
	}

	void RenderGraphComputeLabSession::Update(float deltaTime) noexcept
	{
		const float animationSpeed = m_State->m_AnimationSpeed.load(std::memory_order_relaxed);
		if (std::isfinite(deltaTime) && deltaTime > 0.0f)
		{
			m_AnimationTimeSeconds =
				std::fmod(m_AnimationTimeSeconds + deltaTime * animationSpeed, 6.28318530718f);
			m_State->m_AnimationTimeSeconds.store(
				m_AnimationTimeSeconds, std::memory_order_relaxed);
		}
		GetCamera().Update();
	}

	void RenderGraphComputeLabSession::ApplyImmediateParameters() noexcept
	{
		if (!m_State)
		{
			return;
		}

		const auto& parameters = GetParameters();
		m_State->m_AnimationSpeed.store(
			parameters.Get(AnimationSpeedId, 1.0f), std::memory_order_relaxed);
		m_State->m_PatternFrequency.store(
			parameters.Get(PatternFrequencyId, 18.0f), std::memory_order_relaxed);
		m_State->m_RingRadius.store(parameters.Get(RingRadiusId, 0.46f), std::memory_order_relaxed);
		m_State->m_RingIntensity.store(
			parameters.Get(RingIntensityId, 0.82f), std::memory_order_relaxed);
		m_State->m_CheckerCellSize.store(
			parameters.Get(CheckerCellSizeId, uint32_t(32)), std::memory_order_relaxed);
	}

	void RenderGraphComputeLabSession::BuildDiagnostics(
		LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		diagnostics.m_Title = "RenderGraph Compute-on-Direct Smoke";
		if (!m_State)
		{
			return;
		}

		const uint64_t initializeExecutions =
			m_State->m_InitializeExecutions.load(std::memory_order_relaxed);
		const uint64_t writeExecutions = m_State->m_WriteExecutions.load(std::memory_order_relaxed);
		const uint64_t readWriteExecutions =
			m_State->m_ReadWriteExecutions.load(std::memory_order_relaxed);
		const uint64_t previewExecutions =
			m_State->m_PreviewExecutions.load(std::memory_order_relaxed);
		const uint64_t culledExecutions =
			m_State->m_CulledExecutions.load(std::memory_order_relaxed);
		const bool hasExecuted = previewExecutions > 0;
		const bool chainBalanced = initializeExecutions == writeExecutions &&
			writeExecutions == readWriteExecutions &&
			readWriteExecutions == previewExecutions;

		diagnostics.m_Metrics = {
			{ .m_Name = "Graphics initialize", .m_Value = std::to_string(initializeExecutions) },
			{ .m_Name = "Compute write", .m_Value = std::to_string(writeExecutions) },
			{ .m_Name = "Compute read/write", .m_Value = std::to_string(readWriteExecutions) },
			{ .m_Name = "Graphics preview", .m_Value = std::to_string(previewExecutions) },
			{ .m_Name = "Culled dispatches", .m_Value = std::to_string(culledExecutions) },
		};
		if (const uint64_t probeExecutions = m_State->m_ProbeExecutions.load(std::memory_order_relaxed);
			probeExecutions != 0)
		{
			diagnostics.m_Metrics.push_back({ .m_Name = "Non-finite probe dispatches",
				.m_Value = std::to_string(probeExecutions) });
		}
		diagnostics.m_Checks.push_back({
			.m_Name = "Graphics / direct-compute / graphics chain",
			.m_Status = !hasExecuted ?
				LabDiagnosticCheckStatus::Pending :
				chainBalanced ?
					LabDiagnosticCheckStatus::Passed :
					LabDiagnosticCheckStatus::Failed,
			.m_Detail = !hasExecuted ?
				"Waiting for the first rendered frame." :
				chainBalanced ?
					"All live passes executed exactly once per rendered frame." :
					"Live pass execution counts diverged.",
			});
		diagnostics.m_Checks.push_back({
			.m_Name = "Unused compute pass culling",
			.m_Status = !hasExecuted ?
				LabDiagnosticCheckStatus::Pending :
				culledExecutions == 0 ?
					LabDiagnosticCheckStatus::Passed :
					LabDiagnosticCheckStatus::Failed,
			.m_Detail = culledExecutions == 0 ?
				"The unconsumed UAV writer produced no dispatch." :
				"An unconsumed UAV writer reached execution.",
			});
	}

	LabId RenderGraphComputeLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.render_graph_compute");
	}

	LabDescriptor RenderGraphComputeLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "RenderGraph Compute",
			.m_Category = "Rendering",
			.m_Description =
				"Validates graphics-to-compute transitions, ordered resource-specific UAV barriers, pass culling, and compute results consumed by graphics.",
			.m_Kind = LabKind::Pipeline,
			.m_SchemaVersion = 2,
		};
	}

	std::unique_ptr<LabSessionBase> RenderGraphComputeLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<RenderGraphComputeLabSession>(createInfo);
	}
}
