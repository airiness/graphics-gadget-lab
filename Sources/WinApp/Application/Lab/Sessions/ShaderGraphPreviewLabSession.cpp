#include "Application/Lab/Sessions/ShaderGraphPreviewLabSession.h"
#include "Application/Content/DesktopApplicationContent.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "GGLabFoundation/Base/MathUtils.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/Asset/ReservedTexture.h"
#include "Graphics/Geometry.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RenderPipeline/RenderPipelineOverlayExtensionBase.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/Shader/ShaderProgramCatalog.h"
#include "GGLabRuntime/Scene/Components.h"
#include "ShaderArtifactRuntime/ShaderGraphPreviewProgram.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gglab
{
	struct ShaderGraphPreviewLabState final
	{
		std::atomic<uint32_t> m_ViewMode = 0;
		std::atomic<float> m_Metal = 0.0f;
		std::atomic<float> m_Roughness = 0.5f;
		std::atomic<float> m_TintR = 0.15f;
		std::atomic<float> m_TintG = 0.55f;
		std::atomic<float> m_TintB = 1.0f;
		std::atomic<uint32_t> m_TextureFixture = 0;

		std::atomic<uint64_t> m_PassExecutions = 0;
		std::atomic<uint64_t> m_ExecutedProgramGeneration = 0;
		std::atomic<uint32_t> m_LastDrawCount = 0;
		std::atomic<uint32_t> m_LastTextureIndex = 0;
		std::atomic<uint32_t> m_LastSamplerIndex = 0;
		std::atomic<uint32_t> m_LastContract = 0;
		std::atomic<bool> m_LastParameterBlockValid = false;
	};

	namespace
	{
		enum class PreviewInputContract : int32_t
		{
			NumericV1 = 0,
			Texture2DV2 = 1,
		};

		enum class PreviewTextureFixture : int32_t
		{
			White = 0,
			Checker = 1,
		};

		enum class PreviewPrimitiveIsolation : int32_t
		{
			ShowAll = 0,
			Sphere = 1,
			Plane = 2,
			Cube = 3,
		};

		constexpr RHIFormat PreviewDepthFormat = RHIFormat::D32Float;

		const LabParameterId InputContractId("shader_graph_preview.input_contract");
		const LabParameterId OutputViewId("shader_graph_preview.output_view");
		const LabParameterId MetalId("shader_graph_preview.numeric.metal");
		const LabParameterId TintId("shader_graph_preview.numeric.tint");
		const LabParameterId RoughnessId("shader_graph_preview.texture2d.roughness");
		const LabParameterId TextureFixtureId("shader_graph_preview.texture2d.texture");
		const LabParameterId PrimitiveIsolationId("shader_graph_preview.primitive_isolation");
		const LabParameterId EnableCameraInputId("shader_graph_preview.camera.enable_input");
		const LabParameterId ResetCameraId("shader_graph_preview.camera.reset");

		const RenderPassInfo PreviewPassInfo{
			.m_TypeName = "Lab.ShaderGraphPreview.Render",
			.m_DisplayName = "Shader Graph Preview",
			.m_CategoryName = "Geometry",
			.m_Description =
				"Renders pinned generated Surface functions through the main-owned Preview Program.",
			.m_Category = RenderPassCategory::Geometry,
			.m_Type = RenderPassType::Graphics,
		};

		struct PreviewGraphResources final
		{
			RGTextureId m_BackBuffer{};
			RGTextureId m_Depth{};
		};

		inline constexpr const char* PreviewGraphResourcesName =
			"ShaderGraphPreview.Resources";

		struct PreviewPassData final
		{
			RGTextureViewId m_BackBufferRtv{};
			RGTextureViewId m_DepthDsv{};
			const RenderQueue* m_RenderQueue = nullptr;
		};

		struct FinishPassData final
		{
		};

		[[nodiscard]] PreviewInputContract ResolveInputContract(int32_t value) noexcept
		{
			return value == int32_t(PreviewInputContract::Texture2DV2)
				? PreviewInputContract::Texture2DV2
				: PreviewInputContract::NumericV1;
		}

		[[nodiscard]] PreviewTextureFixture ResolveTextureFixture(int32_t value) noexcept
		{
			return value == int32_t(PreviewTextureFixture::Checker)
				? PreviewTextureFixture::Checker
				: PreviewTextureFixture::White;
		}

		[[nodiscard]] PreviewPrimitiveIsolation ResolvePrimitiveIsolation(
			int32_t value) noexcept
		{
			return value >= int32_t(PreviewPrimitiveIsolation::ShowAll) &&
				value <= int32_t(PreviewPrimitiveIsolation::Cube)
				? static_cast<PreviewPrimitiveIsolation>(value)
				: PreviewPrimitiveIsolation::ShowAll;
		}

		[[nodiscard]] const ShaderProgramRef& ResolvePreviewProgramRef(
			PreviewInputContract contract) noexcept
		{
			return contract == PreviewInputContract::Texture2DV2
				? shader_programs::ShaderGraphPreviewSurfaceV2Pixel
				: shader_programs::ShaderGraphPreviewSurfaceV1Pixel;
		}

		[[nodiscard]] std::string_view ResolvePreviewInputContractId(
			PreviewInputContract contract) noexcept
		{
			return contract == PreviewInputContract::Texture2DV2
				? ShaderGraphPreviewTexture2DInputContractId
				: ShaderGraphPreviewNumericInputContractId;
		}

		[[nodiscard]] std::string_view ResolveGeneratedSourceIdentity(
			PreviewInputContract contract) noexcept
		{
			return contract == PreviewInputContract::Texture2DV2
				? ShaderGraphPreviewTexture2DGeneratedSourceIdentity
				: ShaderGraphPreviewNumericGeneratedSourceIdentity;
		}

		[[nodiscard]] uint32_t ResolveProfileVersion(PreviewInputContract contract) noexcept
		{
			return contract == PreviewInputContract::Texture2DV2 ? 2u : 1u;
		}

		[[nodiscard]] std::string_view ResolveViewModeName(uint32_t value) noexcept
		{
			const auto iterator = std::ranges::find_if(ShaderGraphPreviewViewModes,
				[value](const ShaderGraphPreviewViewModeProjection& mode) noexcept
				{ return static_cast<uint32_t>(mode.m_Value) == value; });
			return iterator != ShaderGraphPreviewViewModes.end() ? iterator->m_Name : "combined";
		}

		[[nodiscard]] TextureID ResolvePreviewTextureId(PreviewTextureFixture fixture) noexcept
		{
			return ToTextureId(fixture == PreviewTextureFixture::Checker
				? ReservedTextureIDIndex::MissingTextureChecker
				: ReservedTextureIDIndex::BaseColorWhite);
		}

		[[nodiscard]] components::MaterialInstanceComponent MakePreviewMaterial(
			std::string_view key) noexcept
		{
			components::MaterialInstanceComponent material{};
			material.m_Key = RuntimeMaterialKey(key);
			material.m_Properties.m_BaseColor = Color::White;
			material.m_Properties.m_RoughnessFactor = 1.0f;
			return material;
		}

		[[nodiscard]] std::string ResolveBackendName(RHIBackendType backend) noexcept
		{
			switch (backend)
			{
			case RHIBackendType::DX12:
				return "DX12";
			case RHIBackendType::Vulkan:
				return "Vulkan";
			case RHIBackendType::Unknown:
				break;
			}
			return "Unknown";
		}

		[[nodiscard]] std::string ResolveTargetName(RHIBackendType backend) noexcept
		{
			return backend == RHIBackendType::Vulkan ? "gglab-vulkan13 / SPIR-V"
				: backend == RHIBackendType::DX12 ? "gglab-dx12 / DXIL"
										 : "Unknown";
		}

		[[nodiscard]] std::string_view ResolveAttachedSessionStateName(
			ShaderPreviewRuntimeSessionState state) noexcept
		{
			switch (state)
			{
			case ShaderPreviewRuntimeSessionState::WaitingForInitialLoad:
				return "WaitingForInitialLoad";
			case ShaderPreviewRuntimeSessionState::Pending:
				return "Pending";
			case ShaderPreviewRuntimeSessionState::Loaded:
				return "Loaded";
			case ShaderPreviewRuntimeSessionState::Rejected:
				return "Rejected / LastGood";
			}
			return "Unknown";
		}

		[[nodiscard]] std::string_view ResolvePreviewRejectionCodeName(
			ShaderPreviewRejectionCode code) noexcept
		{
			switch (code)
			{
			case ShaderPreviewRejectionCode::None:
				return "None";
			case ShaderPreviewRejectionCode::PublicationUnavailable:
				return "PublicationUnavailable";
			case ShaderPreviewRejectionCode::PublicationInvalid:
				return "PublicationInvalid";
			case ShaderPreviewRejectionCode::ShaderArtifactUnavailable:
				return "ShaderArtifactUnavailable";
			case ShaderPreviewRejectionCode::ShaderArtifactInvalid:
				return "ShaderArtifactInvalid";
			case ShaderPreviewRejectionCode::RegistryUnavailable:
				return "RegistryUnavailable";
			case ShaderPreviewRejectionCode::RegistryInvalid:
				return "RegistryInvalid";
			case ShaderPreviewRejectionCode::ActivationFailed:
				return "ActivationFailed";
			case ShaderPreviewRejectionCode::IOFailure:
				return "IOFailure";
			}
			return "Unknown";
		}

		class ShaderGraphPreviewRenderPipeline final : public RenderPipelineBase
		{
		public:
			ShaderGraphPreviewRenderPipeline(std::shared_ptr<ShaderGraphPreviewLabState> state,
				PreviewInputContract contract) noexcept :
				m_State(std::move(state)), m_Contract(contract)
			{
				GGLAB_ASSERT_NOT_NULL(m_State.get());
				m_State->m_PassExecutions.store(0, std::memory_order_relaxed);
				m_State->m_ExecutedProgramGeneration.store(0, std::memory_order_relaxed);
				m_State->m_LastDrawCount.store(0, std::memory_order_relaxed);
				m_State->m_LastContract.store(
					static_cast<uint32_t>(m_Contract), std::memory_order_relaxed);
				m_State->m_LastParameterBlockValid.store(false, std::memory_order_relaxed);
			}

			std::string_view GetName() const noexcept override
			{
				return "Shader Graph Preview Lab";
			}

			void BuildRenderGraph(RenderGraph& rg, const RenderFrameContext& context,
				const RenderServices& services) noexcept override
			{
				GGLAB_ASSERT_MSG(context.IsValid(), "RenderFrameContext invalid.");
				GGLAB_ASSERT_MSG(services.IsValid(), "RenderServices invalid.");
				EnsureInitialized(services);
				GGLAB_ASSERT_MSG(m_IsInitialized,
					"Shader Graph Preview pipeline initialization failed.");
				if (!m_IsInitialized)
				{
					return;
				}

				auto* renderer = services.m_Renderer;
				auto* swapChain = renderer->GetSwapChain();
				const uint32_t backBufferIndex = context.m_BackBufferIndex;
				const uint32_t width = swapChain->GetBufferWidth();
				const uint32_t height = swapChain->GetBufferHeight();
				const uint32_t frameSlotIndex = context.m_FrameSlotIndex;
				const RenderViewID displayViewId = context.GetDisplayViewId();
				const ShaderGraphPreviewPassParameters parameters =
					BuildParameters(context, services);
				const bool parameterBlockValid =
					WriteParameterBlock(frameSlotIndex, parameters);
				m_State->m_LastParameterBlockValid.store(
					parameterBlockValid, std::memory_order_relaxed);

				auto* contextPtr = &context;
				auto* servicesPtr = &services;
				rg.AddPass<PreviewPassData>(PreviewPassInfo.m_TypeName.c_str(),
					[swapChain, backBufferIndex, width, height, displayViewId, contextPtr](
						RenderGraph::RGBuilder& builder, PreviewPassData& data)
					{
						builder.SideEffect();
						auto& resources = builder.GetBlackboard().Create<PreviewGraphResources>(
							PreviewGraphResourcesName);

						RHITextureDesc backBufferDesc{};
						backBufferDesc.m_Format = swapChain->GetFormat();
						backBufferDesc.m_Extent = { width, height, 1 };
						resources.m_BackBuffer = builder.ImportTexture(
							"ShaderGraphPreview.BackBuffer",
							swapChain->GetBackBufferHandle(backBufferIndex), backBufferDesc,
							swapChain->GetBackBufferInitialState(backBufferIndex),
							RGContentValidity::Undefined);

						RHITextureDesc depthDesc{};
						depthDesc.m_Format = PreviewDepthFormat;
						depthDesc.m_Extent = { width, height, 1 };
						depthDesc.m_ClearValue = {
							.m_Format = PreviewDepthFormat,
							.m_Depth = 0.0f,
							.m_IsDepthStencil = true,
						};
						resources.m_Depth =
							builder.CreateTexture("ShaderGraphPreview.Depth", depthDesc);
						builder.WriteInPlace(resources.m_BackBuffer,
							RGTextureAccess::RenderTarget, RHIStage::RenderTarget);
						builder.WriteInPlace(resources.m_Depth,
							RGTextureAccess::DepthStencilWrite, RHIStage::DepthStencil);
						data.m_BackBufferRtv = builder.CreateView<RHITextureViewType::RenderTarget>(
							resources.m_BackBuffer);
						data.m_DepthDsv = builder.CreateView<RHITextureViewType::DepthStencil>(
							resources.m_Depth);
						data.m_RenderQueue =
							std::addressof(contextPtr->GetRenderQueue(displayViewId));

						auto& targets = builder.GetBlackboard()
							.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						targets.m_Width = width;
						targets.m_Height = height;
						targets.m_BackBuffer = resources.m_BackBuffer;
					},
					[this, contextPtr, servicesPtr, frameSlotIndex, parameterBlockValid](
						RGExecuteContext& executeContext, PreviewPassData& data)
					{
						ExecutePreviewPass(executeContext, data, *contextPtr, *servicesPtr,
							frameSlotIndex, parameterBlockValid);
					});

				if (services.m_OverlayExtension)
				{
					services.m_OverlayExtension->AddOverlayPasses(rg, context, services);
				}

				rg.AddPass<FinishPassData>("Lab.ShaderGraphPreview.Finish",
					[displayViewId](RenderGraph::RGBuilder& builder, FinishPassData&)
					{
						builder.SideEffect();
						auto& targets = builder.GetBlackboard()
							.Get<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						builder.Export(targets.m_BackBuffer,
							RGTextureAccess::Present, RHIStage::Present);
					});
			}

		private:
			void EnsureInitialized(const RenderServices& services) noexcept
			{
				if (m_IsInitialized)
				{
					return;
				}

				m_Renderer = services.m_Renderer;
				m_Device = m_Renderer->GetDevice();
				auto* rhiContext = m_Renderer->GetRHIContext();
				auto* shaderManager = services.m_ShaderManager;
				GGLAB_ASSERT_NOT_NULL(m_Device);
				GGLAB_ASSERT_NOT_NULL(rhiContext);
				GGLAB_ASSERT_NOT_NULL(shaderManager);

				m_VertexShader =
					shaderManager->LoadProgram(shader_programs::ForwardCoverageVertex);
				m_PixelShader = shaderManager->LoadProgram(ResolvePreviewProgramRef(m_Contract));

				RHIBindingLayoutDesc bindingLayoutDesc =
					Renderer::BuildCommonRHIBindingLayoutDesc();
				const size_t passSlot =
					static_cast<size_t>(CommonRSRootParamIndex::PassConstants);
				GGLAB_ASSERT(passSlot < bindingLayoutDesc.m_SlotCount);
				bindingLayoutDesc.m_DebugName = "ShaderGraphPreview.BindingLayout";
				bindingLayoutDesc.m_Slots[passSlot] = {
					.m_Type = RHIBindingType::ConstantBuffer,
					.m_Visibility = RHIShaderStage::All,
					.m_Binding = 2,
					.m_Space = 0,
					.m_Count = 1,
					.m_DebugName = "ShaderGraphPreviewPassParameters",
				};
				const RHIBindingLayoutHandle bindingLayout =
					rhiContext->GetPipelineSystem().CreateBindingLayout(bindingLayoutDesc);

				m_BaseRecipe.m_BindingLayout = bindingLayout;
				m_BaseRecipe.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
				m_BaseRecipe.m_VSId = m_VertexShader;
				m_BaseRecipe.m_PSId = m_PixelShader;
				m_BaseRecipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
				m_BaseRecipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
				m_BaseRecipe.m_Formats.m_RenderTargetFormats[0] =
					m_Renderer->GetSwapChain()->GetFormat();
				m_BaseRecipe.m_Formats.m_RenderTargetCount = 1;
				m_BaseRecipe.m_Formats.m_DepthStencilFormat = PreviewDepthFormat;
				m_BaseRecipe.m_Formats.m_SampleCount = 1;
				m_BaseRecipe.m_RasterizerPreset = RasterizerPreset::Default;
				m_BaseRecipe.m_DepthPreset = DepthPreset::ReversedZWrite;
				m_BaseRecipe.m_BlendPreset = BlendPreset::Default;

				const uint64_t requiredAlignment = std::max<uint64_t>(
					m_Device->GetBufferViewAlignment(RHIBufferViewType::ConstantBuffer), 1u);
				m_ParameterBufferSize = utils::AlignUp<uint64_t>(
					sizeof(ShaderGraphPreviewPassParameters), requiredAlignment);
				const uint32_t frameSlotCount = rhiContext->GetFrameSlotCount();
				m_ParameterBuffers.reserve(frameSlotCount);
				for (uint32_t frameSlot = 0; frameSlot < frameSlotCount; ++frameSlot)
				{
					m_ParameterBuffers.emplace_back(m_Device, m_Device->CreateBuffer({
						.m_SizeInBytes = m_ParameterBufferSize,
						.m_Usage = RHIBufferUsage::Constant,
						.m_MemoryUsage = RHIMemoryUsage::CpuToGpu,
						.m_DebugName = "ShaderGraphPreview.PassParameters",
					}));
				}

				m_IsInitialized = m_VertexShader.IsValid() && m_PixelShader.IsValid() &&
					bindingLayout.IsValid() && !m_ParameterBuffers.empty() &&
					std::ranges::all_of(m_ParameterBuffers,
						[](const RHIBufferOwner& buffer) noexcept { return bool(buffer); });
			}

			[[nodiscard]] ShaderGraphPreviewPassParameters BuildParameters(
				const RenderFrameContext& context, const RenderServices& services) noexcept
			{
				ShaderGraphPreviewPassParameters parameters{};
				parameters.ViewIndex =
					static_cast<uint32_t>(utils::ToIndex(context.GetDisplayViewId()));
				parameters.ViewMode = std::min(m_State->m_ViewMode.load(std::memory_order_relaxed),
					static_cast<uint32_t>(ShaderGraphPreviewViewMode::Opacity));
				parameters.Metal =
					std::clamp(m_State->m_Metal.load(std::memory_order_relaxed), 0.0f, 1.0f);
				parameters.Roughness = std::clamp(
					m_State->m_Roughness.load(std::memory_order_relaxed), 0.0f, 1.0f);
				parameters.Tint = {
					m_State->m_TintR.load(std::memory_order_relaxed),
					m_State->m_TintG.load(std::memory_order_relaxed),
					m_State->m_TintB.load(std::memory_order_relaxed),
				};

				if (m_Contract == PreviewInputContract::Texture2DV2)
				{
					const PreviewTextureFixture fixture = static_cast<PreviewTextureFixture>(
						m_State->m_TextureFixture.load(std::memory_order_relaxed));
					parameters.TextureIndex = services.m_AssetManager->ResolveSrvIndex(
						ResolvePreviewTextureId(fixture), ReservedTextureIDIndex::BaseColorWhite);
					parameters.SamplerIndex = services.m_Renderer->GetSamplerRegistry()->GetSamplerIndex(
						SamplerPreset::LinearWrap);
				}

				m_State->m_LastTextureIndex.store(
					parameters.TextureIndex, std::memory_order_relaxed);
				m_State->m_LastSamplerIndex.store(
					parameters.SamplerIndex, std::memory_order_relaxed);
				return parameters;
			}

			[[nodiscard]] bool WriteParameterBlock(uint32_t frameSlotIndex,
				const ShaderGraphPreviewPassParameters& parameters) noexcept
			{
				if (frameSlotIndex >= m_ParameterBuffers.size())
				{
					return false;
				}
				const RHIBufferHandle buffer = m_ParameterBuffers[frameSlotIndex].Get();
				void* mapped = m_Device->MapBuffer(buffer, { 0, 0 });
				if (!mapped)
				{
					return false;
				}
				std::memset(mapped, 0, static_cast<size_t>(m_ParameterBufferSize));
				std::memcpy(mapped, std::addressof(parameters), sizeof(parameters));
				m_Device->UnmapBuffer(buffer, { 0, m_ParameterBufferSize });
				return parameters.Padding == std::array<uint32_t, 3>{};
			}

			void ExecutePreviewPass(RGExecuteContext& executeContext, const PreviewPassData& data,
				const RenderFrameContext& context, const RenderServices& services,
				uint32_t frameSlotIndex, bool parameterBlockValid) noexcept
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				GGLAB_ASSERT_NOT_NULL(commandContext);
				const RHIRenderingAttachment colorAttachment{
					.m_View = executeContext.GetViewHandle(data.m_BackBufferRtv),
					.m_LoadOp = RHIContentLoadOp::DontCare,
				};
				const RHIRenderingAttachment depthAttachment{
					.m_View = executeContext.GetViewHandle(data.m_DepthDsv),
					.m_LoadOp = RHIContentLoadOp::DontCare,
				};
				commandContext->BeginRendering({
					.m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(&colorAttachment, 1),
					.m_DepthAttachment = depthAttachment,
				});
				commandContext->ClearColorAttachment(0, { 0.015f, 0.02f, 0.035f, 1.0f });
				commandContext->ClearDepthAttachment(0.0f);

				uint32_t drawCount = 0;
				const RenderQueue* renderQueue = data.m_RenderQueue;
				if (parameterBlockValid && renderQueue &&
					!renderQueue->m_DrawItems.empty() &&
					renderQueue->m_CoverageRasterDomain.IsValid())
				{
					const auto& ranges = renderQueue->m_BucketDrawRanges;
					const DrawItemsRange* firstRange = nullptr;
					for (const RenderBucket bucket : { RenderBucket::Opaque, RenderBucket::AlphaTest })
					{
						const DrawItemsRange& range = ranges[utils::ToIndex(bucket)];
						if (range.m_Count != 0)
						{
							firstRange = std::addressof(range);
							break;
						}
					}

					if (firstRange && firstRange->m_Start < renderQueue->m_DrawItems.size())
					{
						const uint64_t firstVariant =
							renderQueue->m_DrawItems[firstRange->m_Start].m_VariantBits;
						commandContext->SetPipeline(GetOrCreatePipeline(firstVariant));
						commandContext->SetViewport(
							renderQueue->m_CoverageRasterDomain.m_Viewport);
						commandContext->SetScissorRect(
							renderQueue->m_CoverageRasterDomain.m_Scissor);
						commandContext->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

						const auto* sceneBuffer = m_Renderer->GetSceneConstantBuffer();
						commandContext->SetConstantBuffer(
							static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
							sceneBuffer->GetBufferHandle(),
							context.m_RenderScene.m_SceneConstantBufferOffset);
						commandContext->SetReadOnlyBuffer(
							static_cast<uint32_t>(CommonRSRootParamIndex::ObjectSB),
							m_Renderer->GetObjectStructuredBuffer()->GetBufferHandle(
								context.m_FrameSlotIndex));
						commandContext->SetReadOnlyBuffer(
							static_cast<uint32_t>(CommonRSRootParamIndex::MaterialSB),
							m_Renderer->GetMaterialStructuredBuffer()->GetBufferHandle(
								context.m_FrameSlotIndex));
						commandContext->SetReadOnlyBuffer(
							static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
							m_Renderer->GetViewStructuredBuffer()->GetBufferHandle());
						commandContext->SetReadOnlyBuffer(
							static_cast<uint32_t>(CommonRSRootParamIndex::LightSB),
							m_Renderer->GetLightStructuredBuffer()->GetBufferHandle(
								context.m_FrameSlotIndex));
						commandContext->SetConstantBuffer(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
							m_ParameterBuffers[frameSlotIndex].Get());

						drawCount = DrawPreviewGeometry(
							*commandContext, *renderQueue, firstVariant);
					}
				}

				commandContext->EndRendering();
				m_State->m_PassExecutions.fetch_add(1, std::memory_order_relaxed);
				m_State->m_LastDrawCount.store(drawCount, std::memory_order_relaxed);
				m_State->m_ExecutedProgramGeneration.store(
					services.m_ShaderManager->GetGeneration(m_PixelShader),
					std::memory_order_relaxed);
			}

			[[nodiscard]] uint32_t DrawPreviewGeometry(RHIGraphicsCommandContext& commandContext,
				const RenderQueue& renderQueue, uint64_t firstVariant) noexcept
			{
				uint64_t lastVariant = firstVariant;
				MeshID lastMesh{};
				bool hasBoundMesh = false;
				uint32_t drawCount = 0;
				const auto& ranges = renderQueue.m_BucketDrawRanges;

				for (const RenderBucket bucket : { RenderBucket::Opaque, RenderBucket::AlphaTest })
				{
					const DrawItemsRange& range = ranges[utils::ToIndex(bucket)];
					for (uint32_t index = 0; index < range.m_Count; ++index)
					{
						const DrawItem& drawItem = renderQueue.m_DrawItems[range.m_Start + index];
						const DepthCoverageDrawPacket& packet = drawItem.m_CoverageDrawPacket;
						GGLAB_ASSERT_MSG(packet.IsValid(),
							"Shader Graph Preview received an incomplete draw packet.");

						if (drawItem.m_VariantBits != lastVariant)
						{
							commandContext.SetPipeline(GetOrCreatePipeline(drawItem.m_VariantBits));
							lastVariant = drawItem.m_VariantBits;
						}

						const auto& geometry = packet.m_Geometry;
						if (!hasBoundMesh || geometry.m_MeshId != lastMesh)
						{
							commandContext.SetVertexBuffers(0,
								std::span<const RHIVertexBufferBinding>(
									std::addressof(geometry.m_VertexBuffer), 1));
							commandContext.SetIndexBuffer(geometry.m_IndexBuffer);
							lastMesh = geometry.m_MeshId;
							hasBoundMesh = true;
						}

						commandContext.SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::DrawConstants),
							packet.m_DrawParameters);
						const auto& draw = packet.m_IndexedDraw;
						commandContext.DrawIndexed(draw.m_IndexCount, draw.m_InstanceCount,
							draw.m_StartIndexLocation, draw.m_BaseVertexLocation,
							draw.m_StartInstanceLocation);
						++drawCount;
					}
				}
				return drawCount;
			}

			[[nodiscard]] RHIPipelineHandle GetOrCreatePipeline(uint64_t variantBits) noexcept
			{
				GGLAB_ASSERT((variantBits & ~RenderQueueBuilder::VariantMask) == 0);
				GraphicsPhysicalPipelineKey recipe = m_BaseRecipe;
				recipe.m_RasterizerPreset = RenderQueueBuilder::DecodeVariantDoubleSided(variantBits)
					? RasterizerPreset::TwoSided
					: RasterizerPreset::Default;
				const size_t slotIndex =
					static_cast<size_t>(variantBits & RenderQueueBuilder::VariantMask);
				return m_Renderer->GetPipelineCache()->Resolve(
					m_PipelineSlots[slotIndex], recipe, PreviewPassInfo);
			}

			std::shared_ptr<ShaderGraphPreviewLabState> m_State;
			PreviewInputContract m_Contract = PreviewInputContract::NumericV1;
			Renderer* m_Renderer = nullptr;
			RHIDevice* m_Device = nullptr;
			ShaderID m_VertexShader{};
			ShaderID m_PixelShader{};
			GraphicsPhysicalPipelineKey m_BaseRecipe{};
			std::array<GraphicsPipelineSlot, RenderQueueBuilder::VariantCount> m_PipelineSlots{};
			std::vector<RHIBufferOwner> m_ParameterBuffers;
			uint64_t m_ParameterBufferSize = 0;
			bool m_IsInitialized = false;
		};
	}

	ShaderGraphPreviewLabSession::ShaderGraphPreviewLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, nullptr),
		m_State(std::make_shared<ShaderGraphPreviewLabState>())
	{
		auto& parameters = GetMutableParameters();
		GGLAB_UNUSED(parameters.Add({
			.m_Id = InputContractId,
			.m_Name = "Preview Input Contract",
			.m_Group = "Preview Program",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::RecreatePipeline,
			.m_DefaultValue = int32_t(PreviewInputContract::NumericV1),
			.m_EnumItems = {
				{ int32_t(PreviewInputContract::NumericV1), "Numeric v1" },
				{ int32_t(PreviewInputContract::Texture2DV2), "Texture2D v2" },
			},
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = OutputViewId,
			.m_Name = "Output View",
			.m_Group = "Preview Program",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(ShaderGraphPreviewViewMode::Combined),
			.m_EnumItems = {
				{ int32_t(ShaderGraphPreviewViewMode::Combined), "Combined" },
				{ int32_t(ShaderGraphPreviewViewMode::BaseColor), "Base Color" },
				{ int32_t(ShaderGraphPreviewViewMode::Emissive), "Emissive" },
				{ int32_t(ShaderGraphPreviewViewMode::Metallic), "Metallic" },
				{ int32_t(ShaderGraphPreviewViewMode::Roughness), "Roughness" },
				{ int32_t(ShaderGraphPreviewViewMode::Opacity), "Opacity" },
			},
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = MetalId,
			.m_Name = "Metal",
			.m_Group = "Numeric v1",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.0f,
			.m_MinValue = 0.0f,
			.m_MaxValue = 1.0f,
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = TintId,
			.m_Name = "Tint",
			.m_Group = "Numeric v1",
			.m_Type = LabParameterType::Color,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = Color(0.15f, 0.55f, 1.0f, 1.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = RoughnessId,
			.m_Name = "Roughness",
			.m_Group = "Texture2D v2",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.5f,
			.m_MinValue = 0.0f,
			.m_MaxValue = 1.0f,
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = TextureFixtureId,
			.m_Name = "Texture Fixture",
			.m_Group = "Texture2D v2",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(PreviewTextureFixture::White),
			.m_EnumItems = {
				{ int32_t(PreviewTextureFixture::White), "1x1 White" },
				{ int32_t(PreviewTextureFixture::Checker), "Procedural Checker" },
			},
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = PrimitiveIsolationId,
			.m_Name = "Primitive Visibility",
			.m_Group = "Scene",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(PreviewPrimitiveIsolation::ShowAll),
			.m_EnumItems = {
				{ int32_t(PreviewPrimitiveIsolation::ShowAll), "Show All" },
				{ int32_t(PreviewPrimitiveIsolation::Sphere), "Isolate Sphere" },
				{ int32_t(PreviewPrimitiveIsolation::Plane), "Isolate Plane" },
				{ int32_t(PreviewPrimitiveIsolation::Cube), "Isolate Cube" },
			},
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = EnableCameraInputId,
			.m_Name = "Enable Camera Input",
			.m_Group = "Camera",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = false,
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = ResetCameraId,
			.m_Name = "Reset Camera",
			.m_Group = "Camera",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = false,
		}));

		ApplyImmediateParameters();
		RecreatePipeline();
	}

	void ShaderGraphPreviewLabSession::BeginPrepare() noexcept
	{
		ResetAssetInterests();
		m_AssetPreparation.Reset();
		RebuildScene();
		m_LoadingProgress = m_AssetPreparation.BuildProgress(
			*m_Services.m_AssetManager, "Preparing Shader Graph Preview");
	}

	void ShaderGraphPreviewLabSession::TickPrepare() noexcept
	{
		if (m_LoadingProgress.IsPreparing())
		{
			m_LoadingProgress = m_AssetPreparation.BuildProgress(
				*m_Services.m_AssetManager, "Preparing Shader Graph Preview");
		}
	}

	void ShaderGraphPreviewLabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(m_LoadingProgress.IsReady(),
			"Shader Graph Preview Lab committed before its assets were ready.");
	}

	void ShaderGraphPreviewLabSession::CancelPrepare() noexcept
	{
		ResetAssetInterests();
		m_AssetPreparation.Reset();
		m_World.GetRegistry().clear();
		m_SphereEntity = entt::null;
		m_PlaneEntity = entt::null;
		m_CubeEntity = entt::null;
		m_PrimitivesConstructed = false;
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void ShaderGraphPreviewLabSession::Update(float deltaTime) noexcept
	{
		if (m_EnableCameraInput)
		{
			UpdateCamera(deltaTime);
		}
		else
		{
			GetCamera().Update();
		}
	}

	void ShaderGraphPreviewLabSession::ApplyImmediateParameters() noexcept
	{
		const auto& parameters = GetParameters();
		const int32_t viewMode = parameters.Get(
			OutputViewId, int32_t(ShaderGraphPreviewViewMode::Combined));
		const Color tint = parameters.Get(TintId, Color(0.15f, 0.55f, 1.0f, 1.0f));
		m_State->m_ViewMode.store(static_cast<uint32_t>(std::clamp(viewMode,
			int32_t(ShaderGraphPreviewViewMode::Combined),
			int32_t(ShaderGraphPreviewViewMode::Opacity))), std::memory_order_relaxed);
		m_State->m_Metal.store(parameters.Get(MetalId, 0.0f), std::memory_order_relaxed);
		m_State->m_Roughness.store(
			parameters.Get(RoughnessId, 0.5f), std::memory_order_relaxed);
		m_State->m_TintR.store(tint.m_R, std::memory_order_relaxed);
		m_State->m_TintG.store(tint.m_G, std::memory_order_relaxed);
		m_State->m_TintB.store(tint.m_B, std::memory_order_relaxed);
		m_State->m_TextureFixture.store(static_cast<uint32_t>(ResolveTextureFixture(
			parameters.Get(TextureFixtureId, int32_t(PreviewTextureFixture::White)))),
			std::memory_order_relaxed);
		m_EnableCameraInput = parameters.Get(EnableCameraInputId, false);

		if (parameters.Get(ResetCameraId, false))
		{
			ApplyCameraPreset();
			GGLAB_UNUSED(GetMutableParameters().Set(ResetCameraId, false));
		}
		ApplyPrimitiveVisibility();
	}

	void ShaderGraphPreviewLabSession::RebuildScene() noexcept
	{
		ResetAssetInterests();
		auto& registry = m_World.GetRegistry();
		registry.clear();
		m_SphereEntity = entt::null;
		m_PlaneEntity = entt::null;
		m_CubeEntity = entt::null;
		m_PrimitivesConstructed = false;
		ApplyCameraPreset();

		m_AssetPreparation.TrackModel(ProceduralSphereModelID, "ProceduralSphere", 0.34f);
		m_AssetPreparation.TrackModel(ProceduralPlaneModelID, "ProceduralPlane", 0.33f);
		m_AssetPreparation.TrackModel(ProceduralCubeModelID, "ProceduralCube", 0.33f);

		components::TransformComponent sphereTransform{};
		sphereTransform.m_Position = Vector3(-2.8f, 0.6f, 0.0f);
		sphereTransform.m_Scale = Vector3::One * 1.25f;
		m_SphereEntity = primitive::Sphere::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = sphereTransform,
			.m_MaterialInstance =
				MakePreviewMaterial("gglab.lab.shader_graph_preview.sphere"),
		});

		components::TransformComponent planeTransform{};
		planeTransform.m_Position = Vector3(0.0f, 0.6f, 0.0f);
		planeTransform.m_Scale = Vector3::One * 1.35f;
		m_PlaneEntity = primitive::Plane::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = planeTransform,
			.m_MaterialInstance =
				MakePreviewMaterial("gglab.lab.shader_graph_preview.plane"),
		});

		components::TransformComponent cubeTransform{};
		cubeTransform.m_Position = Vector3(2.8f, 0.6f, 0.0f);
		cubeTransform.m_Scale = Vector3::One * 1.15f;
		m_CubeEntity = primitive::Cube::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = cubeTransform,
			.m_MaterialInstance =
				MakePreviewMaterial("gglab.lab.shader_graph_preview.cube"),
		});

		const auto isConstructed = [&registry](entt::entity entity) noexcept
		{
			return registry.valid(entity) && registry.all_of<components::TransformComponent,
				components::ModelComponent, components::MaterialInstanceComponent>(entity);
		};
		m_PrimitivesConstructed = isConstructed(m_SphereEntity) &&
			isConstructed(m_PlaneEntity) && isConstructed(m_CubeEntity);
		ApplyPrimitiveVisibility();
	}

	void ShaderGraphPreviewLabSession::RecreatePipeline() noexcept
	{
		const PreviewInputContract contract = m_AttachedPublication
			? (m_AttachedPublication->m_PreviewInputContractId ==
					ShaderGraphPreviewTexture2DInputContractId
				? PreviewInputContract::Texture2DV2
				: PreviewInputContract::NumericV1)
			: ResolveInputContract(GetParameters().Get(
				InputContractId, int32_t(PreviewInputContract::NumericV1)));
		SetRenderPipeline(
			std::make_unique<ShaderGraphPreviewRenderPipeline>(m_State, contract));
	}

	void ShaderGraphPreviewLabSession::ApplyCameraPreset() noexcept
	{
		GetCamera().LookAt(Vector3(0.0f, 1.0f, -10.0f), Vector3(0.0f, 0.6f, 0.0f));
		GetCamera().SetFov(55.0f);
		GetCamera().SetNearFar(0.1f, 200.0f);
		GetCamera().Update();
	}

	void ShaderGraphPreviewLabSession::ApplyPrimitiveVisibility() noexcept
	{
		auto& registry = m_World.GetRegistry();
		const PreviewPrimitiveIsolation isolation = ResolvePrimitiveIsolation(GetParameters().Get(
			PrimitiveIsolationId, int32_t(PreviewPrimitiveIsolation::ShowAll)));
		const auto setVisible = [&registry](
			entt::entity entity, ModelID modelId, bool visible) noexcept
		{
			if (!registry.valid(entity))
			{
				return;
			}
			const bool currentlyVisible = registry.all_of<components::ModelComponent>(entity);
			if (visible && !currentlyVisible)
			{
				registry.emplace<components::ModelComponent>(entity, modelId);
			}
			else if (!visible && currentlyVisible)
			{
				registry.remove<components::ModelComponent>(entity);
			}
		};

		setVisible(m_SphereEntity, ProceduralSphereModelID,
			isolation == PreviewPrimitiveIsolation::ShowAll ||
				isolation == PreviewPrimitiveIsolation::Sphere);
		setVisible(m_PlaneEntity, ProceduralPlaneModelID,
			isolation == PreviewPrimitiveIsolation::ShowAll ||
				isolation == PreviewPrimitiveIsolation::Plane);
		setVisible(m_CubeEntity, ProceduralCubeModelID,
			isolation == PreviewPrimitiveIsolation::ShowAll ||
				isolation == PreviewPrimitiveIsolation::Cube);
	}

	void ShaderGraphPreviewLabSession::BuildDiagnostics(
		LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		const bool attached = m_AttachedPublication.has_value() && m_AttachedSession.has_value();
		diagnostics.m_Title = attached
			? "Shader Graph Preview (Attached Session)"
			: "Shader Graph Preview (Standalone)";
		const auto& parameters = GetParameters();
		const PreviewInputContract contract = attached
			? (m_AttachedPublication->m_PreviewInputContractId ==
					ShaderGraphPreviewTexture2DInputContractId
				? PreviewInputContract::Texture2DV2
				: PreviewInputContract::NumericV1)
			: ResolveInputContract(parameters.Get(
				InputContractId, int32_t(PreviewInputContract::NumericV1)));
		const PreviewTextureFixture textureFixture = ResolveTextureFixture(parameters.Get(
			TextureFixtureId, int32_t(PreviewTextureFixture::White)));
		const PreviewPrimitiveIsolation isolation = ResolvePrimitiveIsolation(parameters.Get(
			PrimitiveIsolationId, int32_t(PreviewPrimitiveIsolation::ShowAll)));
		const ShaderProgramRef& programRef = attached
			? m_AttachedPublication->m_ProgramRef
			: ResolvePreviewProgramRef(contract);
		const auto artifactRef = m_Services.m_ShaderManager->ResolveArtifact(programRef);
		const ShaderProgramRegistryArtifactRef registryRef =
			m_Services.m_ShaderManager->GetActiveRegistryRef();
		const RHIBackendType backend = m_Services.m_ShaderManager->GetActiveBackend();
		const uint64_t executions =
			m_State->m_PassExecutions.load(std::memory_order_relaxed);
		const uint64_t generation =
			m_State->m_ExecutedProgramGeneration.load(std::memory_order_relaxed);
		const uint32_t drawCount =
			m_State->m_LastDrawCount.load(std::memory_order_relaxed);
		const uint32_t expectedDrawCount =
			isolation == PreviewPrimitiveIsolation::ShowAll ? 3u : 1u;

		const auto& registry = m_World.GetRegistry();
		const auto fixtureExists = [&registry](entt::entity entity) noexcept
		{
			return registry.valid(entity) &&
				registry.all_of<components::TransformComponent,
					components::MaterialInstanceComponent>(entity);
		};
		const bool fixturesConstructed = m_PrimitivesConstructed &&
			fixtureExists(m_SphereEntity) && fixtureExists(m_PlaneEntity) &&
			fixtureExists(m_CubeEntity);

		const uint32_t expectedTextureIndex = m_Services.m_AssetManager->ResolveSrvIndex(
			ResolvePreviewTextureId(textureFixture), ReservedTextureIDIndex::BaseColorWhite);
		const uint32_t expectedSamplerIndex =
			m_Services.m_Renderer->GetSamplerRegistry()->GetSamplerIndex(SamplerPreset::LinearWrap);
		const bool textureBindingMatches = contract == PreviewInputContract::NumericV1 ||
			(m_State->m_LastTextureIndex.load(std::memory_order_relaxed) == expectedTextureIndex &&
				m_State->m_LastSamplerIndex.load(std::memory_order_relaxed) ==
					expectedSamplerIndex);
		const bool passCurrent = generation > 0 && executions > 0 &&
			drawCount == expectedDrawCount;
		const bool publicationMappingMatches = !attached ||
			(artifactRef && *artifactRef == m_AttachedPublication->m_ShaderArtifactRef &&
				registryRef == m_AttachedPublication->m_PreviewRegistryRef);
		const bool artifactResolved = artifactRef && artifactRef->IsValid() &&
			registryRef.IsValid() && generation > 0 && publicationMappingMatches;

		const std::string artifactIdentity = artifactRef
			? Sha256DigestToHex(artifactRef->m_ArtifactId.m_DurableDigest)
			: "Unavailable";
		const std::string registryIdentity = registryRef.IsValid()
			? Sha256DigestToHex(registryRef.m_RegistryId.m_DurableDigest)
			: "Unavailable";
		const std::string generatedSourceIdentity = attached
			? Sha256DigestToHex(m_AttachedPublication->m_GeneratedSourceIdentity)
			: std::string(ResolveGeneratedSourceIdentity(contract));
		const std::string descriptorIdentity = attached
			? Sha256DigestToHex(m_AttachedPublication->m_PreviewProgramDescriptorIdentity)
			: std::string(ShaderGraphPreviewProgramDescriptorIdentity);
		const std::string publicationIdentity = attached
			? Sha256DigestToHex(
				m_AttachedPublication->m_PublicationId.m_DurableDigest)
			: "N/A (standalone)";
		const std::string candidatePublicationIdentity = attached &&
			m_AttachedSession->m_ObservedPublicationRef.IsValid()
			? Sha256DigestToHex(m_AttachedSession->m_ObservedPublicationRef
				.m_PublicationId.m_DurableDigest)
			: "Unavailable";
		const std::string loadedPublicationIdentity = attached &&
			m_AttachedSession->m_LoadedPublicationRef.IsValid()
			? Sha256DigestToHex(m_AttachedSession->m_LoadedPublicationRef
				.m_PublicationId.m_DurableDigest)
			: "Unavailable";
		const Color tint = parameters.Get(TintId, Color(0.15f, 0.55f, 1.0f, 1.0f));
		const uint32_t viewMode = static_cast<uint32_t>(std::clamp(parameters.Get(
			OutputViewId, int32_t(ShaderGraphPreviewViewMode::Combined)),
			int32_t(ShaderGraphPreviewViewMode::Combined),
			int32_t(ShaderGraphPreviewViewMode::Opacity)));

		diagnostics.m_Metrics = {
			{ .m_Name = "Mode", .m_Value = attached
				? "Attached Shader Editor publication"
				: "Standalone pinned fixture" },
			{ .m_Name = "Session", .m_Value = attached
				? m_AttachedSession->m_SessionId
				: "N/A" },
			{ .m_Name = "Runtime session state", .m_Value = attached
				? std::string(ResolveAttachedSessionStateName(m_AttachedSession->m_State))
				: "Standalone" },
			{ .m_Name = "Eligibility", .m_Value = !attached
				? "Ready"
				: m_AttachedSession->m_State == ShaderPreviewRuntimeSessionState::Loaded
					? "Current"
					: m_AttachedSession->m_State == ShaderPreviewRuntimeSessionState::Rejected
						? "LastGood / Stale"
						: "Pending" },
			{ .m_Name = "Profile", .m_Value = attached
				? std::format("{} v{}", m_AttachedPublication->m_ProfileId,
					m_AttachedPublication->m_ProfileVersion)
				: std::format("gglab.surface v{}", ResolveProfileVersion(contract)) },
			{ .m_Name = "Preview input contract", .m_Value =
				attached ? m_AttachedPublication->m_PreviewInputContractId
					: std::string(ResolvePreviewInputContractId(contract)) },
			{ .m_Name = "Generated source identity", .m_Value =
				generatedSourceIdentity },
			{ .m_Name = "Preview Program descriptor identity", .m_Value =
				descriptorIdentity },
			{ .m_Name = "ProgramRef", .m_Value =
				std::format("{}:{}", programRef.m_ProgramId, programRef.m_VariantId) },
			{ .m_Name = "Entry", .m_Value = std::string(ShaderGraphPreviewProgramEntry) },
			{ .m_Name = "Backend", .m_Value = ResolveBackendName(backend) },
			{ .m_Name = "Target", .m_Value = ResolveTargetName(backend) },
			{ .m_Name = "Shader Artifact ID", .m_Value = artifactIdentity },
			{ .m_Name = "Registry ID", .m_Value = registryIdentity },
			{ .m_Name = "Publication ID", .m_Value = publicationIdentity },
			{ .m_Name = "Candidate Publication ID", .m_Value =
				candidatePublicationIdentity },
			{ .m_Name = "Loaded / LastGood Publication ID", .m_Value =
				loadedPublicationIdentity },
			{ .m_Name = "Runtime activation error", .m_Value = attached &&
				!m_AttachedSession->m_ActivationError.empty()
				? m_AttachedSession->m_ActivationError
				: "None" },
			{ .m_Name = "Runtime rejection code", .m_Value = attached
				? std::string(ResolvePreviewRejectionCodeName(
					m_AttachedSession->m_RejectionCode))
				: "None" },
			{ .m_Name = "Output view", .m_Value =
				std::string(ResolveViewModeName(viewMode)) },
			{ .m_Name = "Numeric v1 values", .m_Value = std::format(
				"metal={:.3f}, tint=({:.3f}, {:.3f}, {:.3f})",
				parameters.Get(MetalId, 0.0f), tint.m_R, tint.m_G, tint.m_B) },
			{ .m_Name = "Texture2D v2 values", .m_Value = std::format(
				"roughness={:.3f}, texture={}", parameters.Get(RoughnessId, 0.5f),
				textureFixture == PreviewTextureFixture::Checker ? "checker" : "white") },
			{ .m_Name = "Preview pass executions", .m_Value = std::to_string(executions) },
		};

		const LabDiagnosticCheckStatus waitingStatus = !m_LoadingProgress.IsReady()
			? LabDiagnosticCheckStatus::Pending
			: LabDiagnosticCheckStatus::Failed;
		diagnostics.m_Checks = {
			{
				.m_Name = attached
					? "Attached Preview publication mapping"
					: "Ordinary Runtime catalog mapping",
				.m_Status = artifactResolved ? LabDiagnosticCheckStatus::Passed : waitingStatus,
				.m_Detail = artifactResolved
					? attached
						? "WinApp validated every publication cross-link and ShaderManager resolves its exact Pixel/PSMain binding."
						: "ShaderManager resolved the selected Pixel/PSMain ProgramRef from the active registry."
					: "Waiting for the selected pinned Preview Program artifact.",
			},
			{
				.m_Name = "Preview parameter block",
				.m_Status = m_State->m_LastParameterBlockValid.load(std::memory_order_relaxed)
					? LabDiagnosticCheckStatus::Passed
					: waitingStatus,
				.m_Detail =
					"The 48-byte b2 constant-buffer upload uses bounded values and explicit zero padding.",
			},
			{
				.m_Name = "Deterministic primitive fixtures",
				.m_Status = fixturesConstructed ? LabDiagnosticCheckStatus::Passed : waitingStatus,
				.m_Detail =
					"GGLab constructed the UV sphere, plane, and cube without third-party assets.",
			},
			{
				.m_Name = "Preview pass current generation",
				.m_Status = passCurrent ? LabDiagnosticCheckStatus::Passed : waitingStatus,
				.m_Detail = passCurrent
					? std::format("Rendered {} selected primitive draw(s) with shader generation {}.",
						drawCount, generation)
					: "Waiting for the selected artifact generation to render every visible fixture.",
			},
			{
				.m_Name = "Texture2D binding pair",
				.m_Status = textureBindingMatches ? LabDiagnosticCheckStatus::Passed : waitingStatus,
				.m_Detail = contract == PreviewInputContract::Texture2DV2
					? "The selected reserved texture SRV and LinearWrap sampler use real production heap indices."
					: "The numeric-v1 contract has no texture/sampler input pair.",
			},
		};
	}

	void ShaderGraphPreviewLabSession::ApplyAttachedRuntimeSession(
		const ShaderPreviewPublicationArtifact& loadedPublication,
		ShaderPreviewRuntimeSessionSnapshot snapshot) noexcept
	{
		const int32_t inputContract = loadedPublication.m_PreviewInputContractId ==
			ShaderGraphPreviewTexture2DInputContractId
			? int32_t(PreviewInputContract::Texture2DV2)
			: int32_t(PreviewInputContract::NumericV1);
		LabChangeImpact impact = LabChangeImpact::Immediate;
		const bool contractChanged = GetParameters().Get(InputContractId, -1) != inputContract &&
			SetParameter(InputContractId, inputContract, &impact);
		m_AttachedPublication = loadedPublication;
		m_AttachedSession = std::move(snapshot);
		if (contractChanged)
		{
			// RecreatePipeline must observe the newly loaded publication rather than
			// the previous attached contract during a live v1/v2 switch.
			ApplyParameterChanges(impact);
		}
	}

	LabId ShaderGraphPreviewLabSession::GetId() noexcept
	{
		return LabId(DesktopShaderGraphPreviewLabId);
	}

	LabDescriptor ShaderGraphPreviewLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Shader Graph Preview",
			.m_Category = "Materials",
			.m_Description =
				"Executes the pinned numeric-v1 and texture2d-v2 Shader Editor emissions through "
				"main-owned Preview Programs on a deterministic sphere, plane, and cube scene.",
			.m_Kind = LabKind::Pipeline,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> ShaderGraphPreviewLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<ShaderGraphPreviewLabSession>(createInfo);
	}
}
