#include "Core/Precompiled.h"
#include "Application/Lab/Sessions/RenderGraphComputeLabSession.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/RenderPassDevelopGui.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/Shader/ShaderManager.h"

namespace gglab
{
	struct RenderGraphComputeLabState
	{
		std::atomic<uint64_t> m_InitializeExecutions = 0;
		std::atomic<uint64_t> m_WriteExecutions = 0;
		std::atomic<uint64_t> m_ReadWriteExecutions = 0;
		std::atomic<uint64_t> m_PreviewExecutions = 0;
		std::atomic<uint64_t> m_CulledExecutions = 0;
	};

	namespace
	{
		constexpr uint32_t WorkTextureWidth = 512;
		constexpr uint32_t WorkTextureHeight = 512;
		constexpr uint32_t ComputeThreadGroupSize = 8;

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
			RGTextureId m_BackBuffer{};
		};

		const char* ComputeLabResourcesName = "RenderGraphComputeLab.Resources";

		struct SetupPassData {};

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

		struct FinishPassData {};

		struct ComputeLabPassParameters
		{
			uint32_t m_WorkAIndex = 0;
			uint32_t m_WorkBIndex = 0;
			uint32_t m_SamplerIndex = 0;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			float m_Phase = 0.0f;
		};
		static_assert(IsPassRootConstantStruct<ComputeLabPassParameters>);
		static_assert(sizeof(ComputeLabPassParameters) == 24);

		RHITextureDesc MakeWorkTextureDesc() noexcept
		{
			RHITextureDesc desc{};
			desc.m_Format = RHIFormat::R16G16B16A16Float;
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

			std::string_view GetName() const noexcept override
			{
				return "RenderGraph Compute Lab";
			}

			void BuildRenderGraph(
				RenderGraph& rg,
				const RenderFrameContext& context,
				const RenderServices& services) noexcept override
			{
				GGLAB_ASSERT_MSG(context.IsValid(), "RenderFrameContext invalid.");
				GGLAB_ASSERT_MSG(services.IsValid(), "RenderServices invalid.");
				EnsureInitialized(services);

				auto* renderer = services.m_Renderer;
				auto* swapChain = renderer->GetSwapChain();
				GGLAB_ASSERT_NOT_NULL(swapChain);

				const uint32_t backBufferIndex = context.m_BackBufferIndex;
				const uint32_t displayWidth = swapChain->GetBufferWidth();
				const uint32_t displayHeight = swapChain->GetBufferHeight();
				const RenderViewID displayViewId = context.GetDisplayViewId();
				const float phase = std::fmod(
					static_cast<float>(m_FrameNumber++) * 0.015f,
					6.28318530718f);

				rg.AddPass<SetupPassData>(
					"Lab.RenderGraphCompute.Setup",
					[swapChain, backBufferIndex, displayWidth, displayHeight, displayViewId](
						RenderGraph::RGBuilder& builder,
						SetupPassData&)
					{
						builder.SideEffect();

						auto& resources = builder.GetBlackboard().Create<ComputeLabResources>(
							ComputeLabResourcesName);
						resources.m_WorkA = builder.CreateTexture(
							"RenderGraphCompute.WorkA",
							MakeWorkTextureDesc());
						resources.m_WorkB = builder.CreateTexture(
							"RenderGraphCompute.WorkB",
							MakeWorkTextureDesc());

						RHITextureDesc backBufferDesc{};
						backBufferDesc.m_Format = swapChain->GetFormat();
						backBufferDesc.m_Extent = { displayWidth, displayHeight, 1u };
						resources.m_BackBuffer = builder.ImportTexture(
							"RenderGraphCompute.BackBuffer",
							swapChain->GetBackBufferHandle(backBufferIndex),
							backBufferDesc,
							RGTextureAccess::Present);

						auto& targets = builder.GetBlackboard()
							.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						targets.m_Width = displayWidth;
						targets.m_Height = displayHeight;
						targets.m_BackBuffer = resources.m_BackBuffer;
					});

				rg.AddPass<InitializePassData>(
					"Lab.RenderGraphCompute.Initialize",
					[](RenderGraph::RGBuilder& builder, InitializePassData& data)
					{
						auto& resources = builder.GetBlackboard().Get<ComputeLabResources>(
							ComputeLabResourcesName);
						builder.WriteInPlace(
							resources.m_WorkA,
							RGTextureAccess::RenderTarget,
							RHIStage::RenderTarget);
						data.m_WorkRtv =
							builder.CreateView<RHITextureViewType::RenderTarget>(
								resources.m_WorkA);
					},
					[state = m_State](
						RGExecuteContext& executeContext,
						InitializePassData& data)
					{
						auto* commandContext =
							executeContext.GetGraphicsCommandContext();
						commandContext->ClearColor(
							executeContext.GetViewHandle(data.m_WorkRtv),
							{ 0.0f, 0.0f, 0.0f, 1.0f });
						state->m_InitializeExecutions.fetch_add(
							1,
							std::memory_order_relaxed);
					});

				rg.AddPass<ComputeWritePassData>(
					ComputeWritePassInfo.m_TypeName.c_str(),
					RGPassEncoderType::Compute,
					[](RenderGraph::RGBuilder& builder, ComputeWritePassData& data)
					{
						auto& resources = builder.GetBlackboard().Get<ComputeLabResources>(
							ComputeLabResourcesName);
						builder.ReadWriteInPlace(
							resources.m_WorkA,
							RGTextureAccess::StorageReadWrite,
							RHIStage::ComputeShader);
						builder.WriteInPlace(
							resources.m_WorkB,
							RGTextureAccess::StorageWrite,
							RHIStage::ComputeShader);
						data.m_WorkAUav =
							builder.CreateView<RHITextureViewType::UnorderedAccess>(
								resources.m_WorkA);
						data.m_WorkBUav =
							builder.CreateView<RHITextureViewType::UnorderedAccess>(
								resources.m_WorkB);
					},
					[this, phase](
						RGExecuteContext& executeContext,
						ComputeWritePassData& data)
					{
						auto* commandContext =
							executeContext.GetDirectComputeCommandContext();
						const auto workAUav =
							executeContext.GetViewDescriptor(data.m_WorkAUav);
						const auto workBUav =
							executeContext.GetViewDescriptor(data.m_WorkBUav);
						GGLAB_ASSERT_MSG(
							workAUav.IsValid() && workBUav.IsValid(),
							"Compute Lab UAVs must be shader visible.");

						commandContext->SetPipeline(GetOrCreateComputeWritePSO());
						commandContext->SetPushConstants(
							static_cast<uint32_t>(
								CommonRSRootParamIndex::PassConstants),
							ComputeLabPassParameters{
								.m_WorkAIndex = workAUav.m_Index,
								.m_WorkBIndex = workBUav.m_Index,
								.m_Width = WorkTextureWidth,
								.m_Height = WorkTextureHeight,
								.m_Phase = phase,
							});
						commandContext->Dispatch(
							(WorkTextureWidth + ComputeThreadGroupSize - 1) /
								ComputeThreadGroupSize,
							(WorkTextureHeight + ComputeThreadGroupSize - 1) /
								ComputeThreadGroupSize,
							1);
						m_State->m_WriteExecutions.fetch_add(
							1,
							std::memory_order_relaxed);
					});

				rg.AddPass<ComputeReadWritePassData>(
					ComputeReadWritePassInfo.m_TypeName.c_str(),
					RGPassEncoderType::Compute,
					[](RenderGraph::RGBuilder& builder, ComputeReadWritePassData& data)
					{
						auto& resources = builder.GetBlackboard().Get<ComputeLabResources>(
							ComputeLabResourcesName);
						builder.ReadWriteInPlace(
							resources.m_WorkA,
							RGTextureAccess::StorageReadWrite,
							RHIStage::ComputeShader);
						data.m_WorkAUav =
							builder.CreateView<RHITextureViewType::UnorderedAccess>(
								resources.m_WorkA);
					},
					[this, phase](
						RGExecuteContext& executeContext,
						ComputeReadWritePassData& data)
					{
						auto* commandContext =
							executeContext.GetDirectComputeCommandContext();
						const auto workAUav =
							executeContext.GetViewDescriptor(data.m_WorkAUav);
						GGLAB_ASSERT_MSG(
							workAUav.IsValid(),
							"Compute Lab read/write UAV must be shader visible.");

						commandContext->SetPipeline(GetOrCreateComputeReadWritePSO());
						commandContext->SetPushConstants(
							static_cast<uint32_t>(
								CommonRSRootParamIndex::PassConstants),
							ComputeLabPassParameters{
								.m_WorkAIndex = workAUav.m_Index,
								.m_Width = WorkTextureWidth,
								.m_Height = WorkTextureHeight,
								.m_Phase = phase,
							});
						commandContext->Dispatch(
							(WorkTextureWidth + ComputeThreadGroupSize - 1) /
								ComputeThreadGroupSize,
							(WorkTextureHeight + ComputeThreadGroupSize - 1) /
								ComputeThreadGroupSize,
							1);
						m_State->m_ReadWriteExecutions.fetch_add(
							1,
							std::memory_order_relaxed);
					});

				rg.AddPass<CulledComputePassData>(
					"Lab.RenderGraphCompute.Culled",
					RGPassEncoderType::Compute,
					[](RenderGraph::RGBuilder& builder, CulledComputePassData& data)
					{
						auto output = builder.CreateTexture(
							"RenderGraphCompute.CulledOutput",
							MakeWorkTextureDesc());
						builder.WriteInPlace(
							output,
							RGTextureAccess::StorageWrite,
							RHIStage::ComputeShader);
						data.m_OutputUav =
							builder.CreateView<RHITextureViewType::UnorderedAccess>(
								output);
					},
					[this](
						RGExecuteContext& executeContext,
						CulledComputePassData& data)
					{
						auto* commandContext =
							executeContext.GetDirectComputeCommandContext();
						const auto outputUav =
							executeContext.GetViewDescriptor(data.m_OutputUav);
						commandContext->SetPipeline(GetOrCreateComputeWritePSO());
						commandContext->SetPushConstants(
							static_cast<uint32_t>(
								CommonRSRootParamIndex::PassConstants),
							ComputeLabPassParameters{
								.m_WorkAIndex = outputUav.m_Index,
								.m_WorkBIndex = outputUav.m_Index,
								.m_Width = WorkTextureWidth,
								.m_Height = WorkTextureHeight,
							});
						commandContext->Dispatch(
							(WorkTextureWidth + ComputeThreadGroupSize - 1) /
								ComputeThreadGroupSize,
							(WorkTextureHeight + ComputeThreadGroupSize - 1) /
								ComputeThreadGroupSize,
							1);
						m_State->m_CulledExecutions.fetch_add(
							1,
							std::memory_order_relaxed);
					});

				const uint32_t samplerIndex =
					renderer->GetSamplerRegistry()->GetSamplerIndex(
						SamplerPreset::LinearClamp);
				rg.AddPass<PreviewPassData>(
					PreviewPassInfo.m_TypeName.c_str(),
					[displayWidth, displayHeight, displayViewId](
						RenderGraph::RGBuilder& builder,
						PreviewPassData& data)
					{
						auto& resources = builder.GetBlackboard().Get<ComputeLabResources>(
							ComputeLabResourcesName);
						auto& targets = builder.GetBlackboard()
							.Get<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						data.m_WorkASrv =
							builder.CreateView<RHITextureViewType::ShaderResource>(
								builder.Read(
									resources.m_WorkA,
									RGTextureAccess::Sample,
									RHIStage::PixelShader));
						data.m_WorkBSrv =
							builder.CreateView<RHITextureViewType::ShaderResource>(
								builder.Read(
									resources.m_WorkB,
									RGTextureAccess::Sample,
									RHIStage::PixelShader));
						builder.WriteInPlace(
							targets.m_BackBuffer,
							RGTextureAccess::RenderTarget,
							RHIStage::RenderTarget);
						resources.m_BackBuffer = targets.m_BackBuffer;
						data.m_BackBufferRtv =
							builder.CreateView<RHITextureViewType::RenderTarget>(
								targets.m_BackBuffer);
						data.m_Width = displayWidth;
						data.m_Height = displayHeight;
					},
					[this, samplerIndex, phase](
						RGExecuteContext& executeContext,
						PreviewPassData& data)
					{
						auto* commandContext =
							executeContext.GetGraphicsCommandContext();
						const auto workASrv =
							executeContext.GetViewDescriptor(data.m_WorkASrv);
						const auto workBSrv =
							executeContext.GetViewDescriptor(data.m_WorkBSrv);
						const auto backBufferRtv =
							executeContext.GetViewHandle(data.m_BackBufferRtv);
						GGLAB_ASSERT_MSG(
							workASrv.IsValid() && workBSrv.IsValid(),
							"Compute Lab preview SRVs must be shader visible.");

						commandContext->ClearColor(
							backBufferRtv,
							{ 0.005f, 0.008f, 0.015f, 1.0f });
						commandContext->SetPipeline(GetOrCreatePreviewPSO());
						commandContext->SetRenderTargets(
							std::span<const RHITextureViewHandle>(
								&backBufferRtv,
								1));
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
						commandContext->SetPushConstants(
							static_cast<uint32_t>(
								CommonRSRootParamIndex::PassConstants),
							ComputeLabPassParameters{
								.m_WorkAIndex = workASrv.m_Index,
								.m_WorkBIndex = workBSrv.m_Index,
								.m_SamplerIndex = samplerIndex,
								.m_Width = data.m_Width,
								.m_Height = data.m_Height,
								.m_Phase = phase,
							});
						commandContext->DrawFullscreenTriangle();
						m_State->m_PreviewExecutions.fetch_add(
							1,
							std::memory_order_relaxed);
					});

				m_DevelopGuiPass.AddPass(rg, context, services);

				rg.AddPass<FinishPassData>(
					"Lab.RenderGraphCompute.Finish",
					[displayViewId](
						RenderGraph::RGBuilder& builder,
						FinishPassData&)
					{
						builder.SideEffect();
						auto& targets = builder.GetBlackboard()
							.Get<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						builder.Export(
							targets.m_BackBuffer,
							RGTextureAccess::Present,
							RHIStage::Present);
					});
			}

		private:
			void EnsureInitialized(const RenderServices& services) noexcept
			{
				if (m_IsInitialized)
				{
					return;
				}

				auto* renderer = services.m_Renderer;
				auto* shaderManager = services.m_ShaderManager;
				GGLAB_ASSERT_NOT_NULL(renderer);
				GGLAB_ASSERT_NOT_NULL(shaderManager);
				m_Renderer = renderer;

				ShaderDesc shaderDesc{};
				shaderDesc.m_SourcePath =
					L"Passes/PassRenderGraphComputeSmoke.hlsl";
				shaderDesc.m_Stage = ShaderStage::Compute;
				shaderDesc.m_Entry = L"CSWrite";
				m_ComputeWriteRecipe.m_CSId =
					shaderManager->LoadShader(shaderDesc);
				shaderDesc.m_Entry = L"CSReadWrite";
				m_ComputeReadWriteRecipe.m_CSId =
					shaderManager->LoadShader(shaderDesc);

				shaderDesc.m_Stage = ShaderStage::Vertex;
				shaderDesc.m_Entry = L"VSMain";
				m_PreviewRecipe.m_VSId =
					shaderManager->LoadShader(shaderDesc);
				shaderDesc.m_Stage = ShaderStage::Pixel;
				shaderDesc.m_Entry = L"PSMain";
				m_PreviewRecipe.m_PSId =
					shaderManager->LoadShader(shaderDesc);

				const auto bindingLayout = renderer->GetCommonBindingLayout();
				m_ComputeWriteRecipe.m_BindingLayout = bindingLayout;
				m_ComputeReadWriteRecipe.m_BindingLayout = bindingLayout;

				m_PreviewRecipe.m_BindingLayout = bindingLayout;
				m_PreviewRecipe.m_InputLayoutId = InputLayoutID::None;
				m_PreviewRecipe.m_TopologyType =
					RHIPrimitiveTopologyType::Triangle;
				m_PreviewRecipe.m_PrimitiveTopology =
					RHIPrimitiveTopology::TriangleList;
				m_PreviewRecipe.m_Formats.m_RenderTargetFormats[0] =
					renderer->GetSwapChain()->GetFormat();
				m_PreviewRecipe.m_Formats.m_RenderTargetCount = 1;
				m_PreviewRecipe.m_Formats.m_DepthStencilFormat =
					RHIFormat::Unknown;
				m_PreviewRecipe.m_Formats.m_SampleCount = 1;
				m_PreviewRecipe.m_RasterizerPreset =
					RasterizerPreset::Default;
				m_PreviewRecipe.m_BlendPreset = BlendPreset::Default;
				m_PreviewRecipe.m_DepthPreset = DepthPreset::DepthDisabled;
				m_IsInitialized = true;
			}

			RHIPipelineHandle GetOrCreateComputeWritePSO() noexcept
			{
				auto* pipelineCache = m_Renderer->GetPipelineCache();
				GGLAB_ASSERT_NOT_NULL(pipelineCache);
				return pipelineCache->Resolve(
					m_ComputeWriteSlot,
					m_ComputeWriteRecipe,
					ComputeWritePassInfo);
			}

			RHIPipelineHandle GetOrCreateComputeReadWritePSO() noexcept
			{
				auto* pipelineCache = m_Renderer->GetPipelineCache();
				GGLAB_ASSERT_NOT_NULL(pipelineCache);
				return pipelineCache->Resolve(
					m_ComputeReadWriteSlot,
					m_ComputeReadWriteRecipe,
					ComputeReadWritePassInfo);
			}

			RHIPipelineHandle GetOrCreatePreviewPSO() noexcept
			{
				auto* pipelineCache = m_Renderer->GetPipelineCache();
				GGLAB_ASSERT_NOT_NULL(pipelineCache);
				return pipelineCache->Resolve(
					m_PreviewSlot,
					m_PreviewRecipe,
					PreviewPassInfo);
			}

			std::shared_ptr<RenderGraphComputeLabState> m_State;
			Renderer* m_Renderer = nullptr;
			RenderPassDevelopGui m_DevelopGuiPass;
			ComputePipelineRecipe m_ComputeWriteRecipe{};
			ComputePipelineRecipe m_ComputeReadWriteRecipe{};
			GraphicsPipelineRecipe m_PreviewRecipe{};
			ComputePipelineSlot m_ComputeWriteSlot{};
			ComputePipelineSlot m_ComputeReadWriteSlot{};
			GraphicsPipelineSlot m_PreviewSlot{};
			uint64_t m_FrameNumber = 0;
			bool m_IsInitialized = false;
		};
	}

	RenderGraphComputeLabSession::RenderGraphComputeLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(
			GetDescriptor(),
			createInfo,
			nullptr),
		m_State(std::make_shared<RenderGraphComputeLabState>())
	{
		SetRenderPipeline(
			std::make_unique<RenderGraphComputeLabPipeline>(m_State));
	}

	void RenderGraphComputeLabSession::Update(float deltaTime) noexcept
	{
		GGLAB_UNUSED(deltaTime);
		GetCamera().Update();
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
		const uint64_t writeExecutions =
			m_State->m_WriteExecutions.load(std::memory_order_relaxed);
		const uint64_t readWriteExecutions =
			m_State->m_ReadWriteExecutions.load(std::memory_order_relaxed);
		const uint64_t previewExecutions =
			m_State->m_PreviewExecutions.load(std::memory_order_relaxed);
		const uint64_t culledExecutions =
			m_State->m_CulledExecutions.load(std::memory_order_relaxed);
		const bool hasExecuted = previewExecutions > 0;
		const bool chainBalanced =
			initializeExecutions == writeExecutions &&
			writeExecutions == readWriteExecutions &&
			readWriteExecutions == previewExecutions;

		diagnostics.m_Metrics = {
			{ .m_Name = "Graphics initialize", .m_Value = std::to_string(initializeExecutions) },
			{ .m_Name = "Compute write", .m_Value = std::to_string(writeExecutions) },
			{ .m_Name = "Compute read/write", .m_Value = std::to_string(readWriteExecutions) },
			{ .m_Name = "Graphics preview", .m_Value = std::to_string(previewExecutions) },
			{ .m_Name = "Culled dispatches", .m_Value = std::to_string(culledExecutions) },
		};
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
			.m_Description = "Validates graphics-to-compute transitions, ordered resource-specific UAV barriers, pass culling, and compute results consumed by graphics.",
			.m_Kind = LabKind::Pipeline,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> RenderGraphComputeLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<RenderGraphComputeLabSession>(createInfo);
	}
}
