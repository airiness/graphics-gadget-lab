#include "Core/Precompiled.h"
#include "Application/Lab/Sessions/CoordinateConformanceLabSession.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/RenderPassDevelopGui.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/Shader/ShaderManager.h"

#include <algorithm>

namespace gglab
{
	struct CoordinateConformanceLabState
	{
		std::atomic<uint64_t> m_MarkerExecutions = 0;
		std::atomic<uint64_t> m_ConformanceExecutions = 0;
	};

	namespace
	{
		constexpr RHIFormat MarkerFormat = RHIFormat::R8G8B8A8Unorm;
		constexpr RHIFormat DepthFormat = RHIFormat::D32Float;
		constexpr uint32_t CullingPanelInsetPixels = 16;

		const RenderPassInfo MarkerPassInfo{
			.m_TypeName = "Lab.CoordinateConformance.Marker",
			.m_DisplayName = "Coordinate Marker",
			.m_CategoryName = "Debug",
			.m_Description = "Generates an upper-left-origin texture marker.",
			.m_Category = RenderPassCategory::Debug,
			.m_Type = RenderPassType::Graphics,
		};

		const RenderPassInfo ConformancePassInfo{
			.m_TypeName = "Lab.CoordinateConformance.Draw",
			.m_DisplayName = "Coordinate Conformance",
			.m_CategoryName = "Debug",
			.m_Description = "Draws geometry, texture, depth, scissor and position probes.",
			.m_Category = RenderPassCategory::Debug,
			.m_Type = RenderPassType::Graphics,
		};

		struct CoordinateConformanceResources
		{
			RGTextureId m_Marker{};
			RGTextureId m_Depth{};
			RGTextureId m_BackBuffer{};
		};

		const char* CoordinateConformanceResourcesName = "CoordinateConformance.Resources";

		struct SetupPassData
		{
		};

		struct MarkerPassData
		{
			RGTextureViewId m_Rtv{};
		};

		struct ConformancePassData
		{
			RGTextureViewId m_MarkerSrv{};
			RGTextureViewId m_BackBufferRtv{};
			RGTextureViewId m_DepthDsv{};
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};

		struct FinishPassData
		{
		};

		enum class CoordinateConformanceMode : uint32_t
		{
			Winding = 0,
			MarkerSampling = 1,
			DepthVisualization = 2,
			Position = 3,
			DepthProbe = 4,
		};
		constexpr float ReversedZFarProbeDepth = 0.25f;
		constexpr float ReversedZNearProbeDepth = 0.75f;

		struct CoordinateConformanceParameters
		{
			uint32_t m_TextureIndex = 0;
			uint32_t m_SamplerIndex = 0;
			CoordinateConformanceMode m_Mode = CoordinateConformanceMode::Winding;
			float m_Depth = 0.0f;
			float m_TargetExtent[2] = { 1.0f, 1.0f };
			float m_DepthOverride = 0.0f;
			float m_Padding = 0.0f;
		};
		static_assert(IsPassRootConstantStruct<CoordinateConformanceParameters>);
		static_assert(sizeof(CoordinateConformanceParameters) == 32);

		struct CoordinateVertex
		{
			float m_Position[3];
			float m_UV[2];
		};
		static_assert(sizeof(CoordinateVertex) == 20);

		class CoordinateConformanceRenderPipeline final : public RenderPipelineBase
		{
		public:
			explicit CoordinateConformanceRenderPipeline(
				std::shared_ptr<CoordinateConformanceLabState> state) noexcept :
				m_State(std::move(state))
			{
				GGLAB_ASSERT_NOT_NULL(m_State.get());
			}

			~CoordinateConformanceRenderPipeline() override
			{
				if (m_Device != nullptr && m_Sampler.IsValid())
				{
					m_Device->DestroySampler(m_Sampler);
				}
			}

			std::string_view GetName() const noexcept override
			{
				return "Coordinate Conformance Lab";
			}

			void BuildRenderGraph(RenderGraph& rg, const RenderFrameContext& context,
				const RenderServices& services) noexcept override
			{
				GGLAB_ASSERT_MSG(context.IsValid(), "RenderFrameContext invalid.");
				GGLAB_ASSERT_MSG(services.IsValid(), "RenderServices invalid.");
				EnsureInitialized(services);

				auto* renderer = services.m_Renderer;
				auto* swapChain = renderer->GetSwapChain();
				const uint32_t width = swapChain->GetBufferWidth();
				const uint32_t height = swapChain->GetBufferHeight();
				const uint32_t backBufferIndex = context.m_BackBufferIndex;
				const RenderViewID displayViewId = context.GetDisplayViewId();

				rg.AddPass<SetupPassData>("Lab.CoordinateConformance.Setup",
					[swapChain, backBufferIndex, width, height, displayViewId](
						RenderGraph::RGBuilder& builder, SetupPassData&)
					{
						builder.SideEffect();
						auto& resources = builder.GetBlackboard().Create<
							CoordinateConformanceResources>(CoordinateConformanceResourcesName);
						resources.m_Marker = builder.CreateTexture("CoordinateConformance.Marker", {
							.m_Format = MarkerFormat,
							.m_Extent = { 2, 2, 1 },
						});
						resources.m_Depth = builder.CreateTexture("CoordinateConformance.Depth", {
							.m_Format = DepthFormat,
							.m_Extent = { width, height, 1 },
							.m_ClearValue = RHIClearValue{
								.m_Format = DepthFormat,
								.m_Depth = 0.0f,
								.m_IsDepthStencil = true,
							},
						});
						RHITextureDesc backBufferDesc{};
						backBufferDesc.m_Format = swapChain->GetFormat();
						backBufferDesc.m_Extent = { width, height, 1 };
						resources.m_BackBuffer = builder.ImportTexture(
							"CoordinateConformance.BackBuffer",
							swapChain->GetBackBufferHandle(backBufferIndex), backBufferDesc,
							RGTextureAccess::Present, RGContentValidity::Undefined);

						auto& targets = builder.GetBlackboard()
							.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						targets.m_Width = width;
						targets.m_Height = height;
						targets.m_BackBuffer = resources.m_BackBuffer;
					});

				rg.AddPass<MarkerPassData>(MarkerPassInfo.m_TypeName.c_str(),
					[](RenderGraph::RGBuilder& builder, MarkerPassData& data)
					{
						auto& resources = builder.GetBlackboard().Get<
							CoordinateConformanceResources>(CoordinateConformanceResourcesName);
						builder.WriteInPlace(resources.m_Marker, RGTextureAccess::RenderTarget,
							RHIStage::RenderTarget);
						data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(
							resources.m_Marker);
					},
					[this](RGExecuteContext& executeContext, MarkerPassData& data)
					{
						auto* commandContext = executeContext.GetGraphicsCommandContext();
						const RHIRenderingAttachment attachment{
							.m_View = executeContext.GetViewHandle(data.m_Rtv),
							.m_LoadOp = RHIContentLoadOp::DontCare,
						};
						commandContext->BeginRendering({ .m_ColorAttachments =
							std::span<const RHIRenderingAttachment>(&attachment, 1) });
						commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
						commandContext->SetPipeline(GetOrCreateMarkerPipeline());
						commandContext->SetViewport({ 0.0f, 0.0f, 2.0f, 2.0f });
						commandContext->SetScissorRect({ 0, 0, 2, 2 });
						commandContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
							CoordinateConformanceParameters{});
						commandContext->DrawFullscreenTriangle();
						commandContext->EndRendering();
						m_State->m_MarkerExecutions.fetch_add(1, std::memory_order_relaxed);
					});

				rg.AddPass<ConformancePassData>(ConformancePassInfo.m_TypeName.c_str(),
					[width, height, displayViewId](
						RenderGraph::RGBuilder& builder, ConformancePassData& data)
					{
						auto& resources = builder.GetBlackboard().Get<
							CoordinateConformanceResources>(CoordinateConformanceResourcesName);
						data.m_MarkerSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
							builder.Read(resources.m_Marker, RGTextureAccess::Sample,
								RHIStage::PixelShader));
						builder.WriteInPlace(resources.m_BackBuffer, RGTextureAccess::RenderTarget,
							RHIStage::RenderTarget);
						builder.WriteInPlace(resources.m_Depth, RGTextureAccess::DepthStencilWrite,
							RHIStage::DepthStencil);
						data.m_BackBufferRtv = builder.CreateView<RHITextureViewType::RenderTarget>(
							resources.m_BackBuffer);
						data.m_DepthDsv = builder.CreateView<RHITextureViewType::DepthStencil>(
							resources.m_Depth);
						data.m_Width = width;
						data.m_Height = height;
						auto& targets = builder.GetBlackboard()
							.Get<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						targets.m_BackBuffer = resources.m_BackBuffer;
					},
					[this](RGExecuteContext& executeContext, ConformancePassData& data)
					{
						auto* commandContext = executeContext.GetGraphicsCommandContext();
						const std::array attachments{
							RHIRenderingAttachment{
								.m_View = executeContext.GetViewHandle(data.m_BackBufferRtv),
								.m_LoadOp = RHIContentLoadOp::DontCare,
							},
						};
						const RHIRenderingAttachment depthAttachment{
							.m_View = executeContext.GetViewHandle(data.m_DepthDsv),
							.m_LoadOp = RHIContentLoadOp::DontCare,
						};
						commandContext->BeginRendering({
							.m_ColorAttachments = attachments,
							.m_DepthAttachment = depthAttachment,
						});
						commandContext->ClearColorAttachment(0, { 0.01f, 0.015f, 0.025f, 1.0f });
						commandContext->ClearDepthAttachment(0.0f);

						const RHIDescriptorHandle marker =
							executeContext.GetViewDescriptor(data.m_MarkerSrv);
						GGLAB_ASSERT_MSG(marker.IsValid() && m_SamplerDescriptor.IsValid(),
							"Coordinate conformance descriptors must be shader visible.");
						const uint32_t halfWidth = data.m_Width / 2;
						const uint32_t halfHeight = data.m_Height / 2;
						const uint32_t rightWidth = data.m_Width - halfWidth;
						const uint32_t lowerHeight = data.m_Height - halfHeight;
						const uint32_t panelInset =
							rightWidth > 2 * CullingPanelInsetPixels &&
							lowerHeight > 2 * CullingPanelInsetPixels ? CullingPanelInsetPixels : 0;
						const uint32_t panelLeft = halfWidth + panelInset;
						const uint32_t panelTop = halfHeight + panelInset;
						const uint32_t panelRight = data.m_Width - panelInset;
						const uint32_t panelBottom = data.m_Height - panelInset;
						const auto makeParameters = [&](CoordinateConformanceMode mode,
							float depth = 0.0f, float depthOverride = 0.0f)
						{
							return CoordinateConformanceParameters{
								.m_TextureIndex = marker.m_Index,
								.m_SamplerIndex = m_SamplerDescriptor.m_Index,
								.m_Mode = mode,
								.m_Depth = depth,
								.m_TargetExtent = {
									static_cast<float>(data.m_Width),
									static_cast<float>(data.m_Height),
								},
								.m_DepthOverride = depthOverride,
							};
						};

						const RHIVertexBufferBinding vertexBinding{
							.m_Buffer = m_VertexBuffer.Get(),
							.m_Stride = sizeof(CoordinateVertex),
							.m_SizeInBytes = static_cast<uint32_t>(sizeof(CoordinateVertex) * 10),
						};
						commandContext->SetVertexBuffers(
							0, std::span<const RHIVertexBufferBinding>(&vertexBinding, 1));
						commandContext->SetPipeline(GetOrCreateGeometryPipeline());
						commandContext->SetViewport({ 0.0f, 0.0f,
							static_cast<float>(halfWidth), static_cast<float>(halfHeight) });
						commandContext->SetScissorRect({ 0, 0,
							static_cast<int32_t>(halfWidth), static_cast<int32_t>(halfHeight) });
						commandContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
							makeParameters(CoordinateConformanceMode::Winding));
						commandContext->Draw(6);

						const RHIIndexBufferBinding indexBinding{
							.m_Buffer = m_IndexBuffer.Get(),
							.m_SizeInBytes = 6 * sizeof(uint32_t),
							.m_Format = RHIFormat::R32Uint,
						};
						commandContext->SetIndexBuffer(indexBinding);
						commandContext->SetViewport({ static_cast<float>(halfWidth), 0.0f,
							static_cast<float>(data.m_Width - halfWidth),
							static_cast<float>(halfHeight) });
						commandContext->SetScissorRect({ static_cast<int32_t>(halfWidth), 0,
							static_cast<int32_t>(data.m_Width), static_cast<int32_t>(halfHeight) });
						commandContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
							makeParameters(CoordinateConformanceMode::MarkerSampling));
						commandContext->DrawIndexed(6, 1, 0, 6, 0);

						commandContext->SetPipeline(GetOrCreateDepthPipeline());
						commandContext->SetViewport({ 0.0f, static_cast<float>(halfHeight),
							static_cast<float>(halfWidth), static_cast<float>(data.m_Height - halfHeight) });
						commandContext->SetScissorRect({ 0, static_cast<int32_t>(halfHeight),
							static_cast<int32_t>(halfWidth), static_cast<int32_t>(data.m_Height) });
						commandContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
							makeParameters(CoordinateConformanceMode::DepthVisualization,
								ReversedZFarProbeDepth));
						commandContext->DrawFullscreenTriangle();
						commandContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
							makeParameters(CoordinateConformanceMode::DepthVisualization,
								ReversedZNearProbeDepth));
						commandContext->DrawFullscreenTriangle();

						commandContext->SetPipeline(GetOrCreatePositionPipeline());
						commandContext->SetViewport({ static_cast<float>(halfWidth),
							static_cast<float>(halfHeight), static_cast<float>(rightWidth),
							static_cast<float>(lowerHeight) });
						commandContext->SetScissorRect({ static_cast<int32_t>(panelLeft),
							static_cast<int32_t>(panelTop), static_cast<int32_t>(panelRight),
							static_cast<int32_t>(panelBottom) });
						commandContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
							makeParameters(CoordinateConformanceMode::Position));
						commandContext->DrawFullscreenTriangle();

						// Overlay the same two-sided winding pair on the position gradient. With
						// back-face culling enabled, the surviving triangle remains solid while
						// the gradient stays visible where its opposite-facing pair was rejected.
						commandContext->SetPipeline(GetOrCreateBackCullPipeline());
						commandContext->SetViewport({ static_cast<float>(panelLeft),
							static_cast<float>(panelTop), static_cast<float>(panelRight - panelLeft),
							static_cast<float>(panelBottom - panelTop) });
						commandContext->SetScissorRect({ static_cast<int32_t>(panelLeft),
							static_cast<int32_t>(panelTop), static_cast<int32_t>(panelRight),
							static_cast<int32_t>(panelBottom) });
						commandContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
							makeParameters(CoordinateConformanceMode::Winding));
						commandContext->Draw(6);
						commandContext->EndRendering();
						m_State->m_ConformanceExecutions.fetch_add(1, std::memory_order_relaxed);
					});

				m_DevelopGuiPass.AddPass(rg, context, services);
				rg.AddPass<FinishPassData>("Lab.CoordinateConformance.Finish",
					[displayViewId](RenderGraph::RGBuilder& builder, FinishPassData&)
					{
						builder.SideEffect();
						auto& targets = builder.GetBlackboard()
							.Get<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						builder.Export(targets.m_BackBuffer, RGTextureAccess::Present,
							RHIStage::Present);
					});
			}

		private:
			void EnsureInitialized(const RenderServices& services) noexcept
			{
				if (m_Initialized)
				{
					return;
				}
				m_Renderer = services.m_Renderer;
				m_Device = m_Renderer->GetDevice();
				auto* shaderManager = services.m_ShaderManager;
				GGLAB_ASSERT_NOT_NULL(m_Device);
				GGLAB_ASSERT_NOT_NULL(shaderManager);

				ShaderDesc shaderDesc{};
				shaderDesc.m_SourcePath = L"Passes/PassCoordinateConformance.hlsl";
				shaderDesc.m_Stage = ShaderStage::Vertex;
				shaderDesc.m_Entry = L"VSGeometry";
				const ShaderID geometryVS = shaderManager->LoadShader(shaderDesc);
				shaderDesc.m_Entry = L"VSFullscreen";
				const ShaderID fullscreenVS = shaderManager->LoadShader(shaderDesc);
				shaderDesc.m_Stage = ShaderStage::Pixel;
				shaderDesc.m_Entry = L"PSMarker";
				const ShaderID markerPS = shaderManager->LoadShader(shaderDesc);
				shaderDesc.m_Entry = L"PSConformance";
				const ShaderID conformancePS = shaderManager->LoadShader(shaderDesc);

				const RHIBindingLayoutHandle bindingLayout = m_Renderer->GetCommonBindingLayout();
				const RHIFormat backBufferFormat = m_Renderer->GetSwapChain()->GetFormat();
				const auto initializeRecipe = [bindingLayout](GraphicsPhysicalPipelineKey& recipe,
					ShaderID vertexShader, ShaderID pixelShader, InputLayoutID inputLayout,
					RHIFormat colorFormat, RHIFormat depthFormat, DepthPreset depthPreset) noexcept
				{
					recipe.m_BindingLayout = bindingLayout;
					recipe.m_VSId = vertexShader;
					recipe.m_PSId = pixelShader;
					recipe.m_InputLayoutId = inputLayout;
					recipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
					recipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
					recipe.m_Formats.m_RenderTargetFormats[0] = colorFormat;
					recipe.m_Formats.m_RenderTargetCount = 1;
					recipe.m_Formats.m_DepthStencilFormat = depthFormat;
					recipe.m_RasterizerPreset = RasterizerPreset::Default;
					recipe.m_BlendPreset = BlendPreset::Default;
					recipe.m_DepthPreset = depthPreset;
				};
				initializeRecipe(m_MarkerRecipe, fullscreenVS, markerPS, InputLayoutID::None,
					MarkerFormat, RHIFormat::Unknown, DepthPreset::DepthDisabled);
				initializeRecipe(m_GeometryRecipe, geometryVS, conformancePS, InputLayoutID::P3T2,
					backBufferFormat, DepthFormat, DepthPreset::DepthDisabled);
				m_GeometryRecipe.m_RasterizerPreset = RasterizerPreset::TwoSided;
				m_BackCullRecipe = m_GeometryRecipe;
				m_BackCullRecipe.m_RasterizerPreset = RasterizerPreset::Default;
				initializeRecipe(m_DepthRecipe, fullscreenVS, conformancePS, InputLayoutID::None,
					backBufferFormat, DepthFormat, DepthPreset::ReversedZWrite);
				initializeRecipe(m_PositionRecipe, fullscreenVS, conformancePS, InputLayoutID::None,
					backBufferFormat, DepthFormat, DepthPreset::DepthDisabled);

				constexpr std::array vertices{
					CoordinateVertex{{-0.9f, 0.75f, 0.5f}, {0.0f, 0.0f}},
					CoordinateVertex{{-0.1f, -0.7f, 0.5f}, {1.0f, 1.0f}},
					CoordinateVertex{{-0.75f, -0.55f, 0.5f}, {0.0f, 1.0f}},
					CoordinateVertex{{0.1f, 0.75f, 0.5f}, {0.0f, 0.0f}},
					CoordinateVertex{{0.25f, -0.55f, 0.5f}, {0.0f, 1.0f}},
					CoordinateVertex{{0.9f, -0.7f, 0.5f}, {1.0f, 1.0f}},
					CoordinateVertex{{-1.0f, 1.0f, 0.5f}, {0.0f, 0.0f}},
					CoordinateVertex{{1.0f, 1.0f, 0.5f}, {1.0f, 0.0f}},
					CoordinateVertex{{1.0f, -1.0f, 0.5f}, {1.0f, 1.0f}},
					CoordinateVertex{{-1.0f, -1.0f, 0.5f}, {0.0f, 1.0f}},
				};
				constexpr std::array<uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };
				m_VertexBuffer = CreateUploadBuffer(vertices, RHIBufferUsage::Vertex,
					"CoordinateConformance.Vertices");
				m_IndexBuffer = CreateUploadBuffer(indices, RHIBufferUsage::Index,
					"CoordinateConformance.Indices");
				m_Sampler = m_Device->CreateSampler({ .m_Filter = RHISamplerFilter::MinMagMipPoint });
				m_SamplerDescriptor = m_Device->GetSamplerDescriptor(m_Sampler);
				GGLAB_ASSERT_MSG(m_VertexBuffer && m_IndexBuffer && m_SamplerDescriptor.IsValid(),
					"Coordinate Conformance Lab resource initialization failed.");
				m_Initialized = true;
			}

			template <typename T, size_t Size>
			RHIBufferOwner CreateUploadBuffer(const std::array<T, Size>& data,
				RHIBufferUsage usage, const char* debugName) noexcept
			{
				const uint64_t sizeInBytes = sizeof(data);
				RHIBufferOwner buffer(m_Device, m_Device->CreateBuffer({
					.m_SizeInBytes = sizeInBytes,
					.m_StrideInBytes = sizeof(T),
					.m_Usage = usage,
					.m_MemoryUsage = RHIMemoryUsage::CpuToGpu,
					.m_DebugName = debugName,
				}));
				void* mapped = m_Device->MapBuffer(buffer.Get(), { 0, 0 });
				if (mapped == nullptr)
				{
					return {};
				}
				std::memcpy(mapped, data.data(), sizeInBytes);
				m_Device->UnmapBuffer(buffer.Get(), { 0, sizeInBytes });
				return buffer;
			}

			RHIPipelineHandle GetOrCreateMarkerPipeline() noexcept
			{
				return m_Renderer->GetPipelineCache()->Resolve(
					m_MarkerSlot, m_MarkerRecipe, MarkerPassInfo);
			}

			RHIPipelineHandle GetOrCreateGeometryPipeline() noexcept
			{
				return m_Renderer->GetPipelineCache()->Resolve(
					m_GeometrySlot, m_GeometryRecipe, ConformancePassInfo);
			}

			RHIPipelineHandle GetOrCreateDepthPipeline() noexcept
			{
				return m_Renderer->GetPipelineCache()->Resolve(
					m_DepthSlot, m_DepthRecipe, ConformancePassInfo);
			}

			RHIPipelineHandle GetOrCreateBackCullPipeline() noexcept
			{
				return m_Renderer->GetPipelineCache()->Resolve(
					m_BackCullSlot, m_BackCullRecipe, ConformancePassInfo);
			}

			RHIPipelineHandle GetOrCreatePositionPipeline() noexcept
			{
				return m_Renderer->GetPipelineCache()->Resolve(
					m_PositionSlot, m_PositionRecipe, ConformancePassInfo);
			}

			std::shared_ptr<CoordinateConformanceLabState> m_State;
			Renderer* m_Renderer = nullptr;
			RHIDevice* m_Device = nullptr;
			RenderPassDevelopGui m_DevelopGuiPass;
			RHIBufferOwner m_VertexBuffer;
			RHIBufferOwner m_IndexBuffer;
			RHISamplerHandle m_Sampler{};
			RHIDescriptorHandle m_SamplerDescriptor{};
			GraphicsPhysicalPipelineKey m_MarkerRecipe{};
			GraphicsPhysicalPipelineKey m_GeometryRecipe{};
			GraphicsPhysicalPipelineKey m_BackCullRecipe{};
			GraphicsPhysicalPipelineKey m_DepthRecipe{};
			GraphicsPhysicalPipelineKey m_PositionRecipe{};
			GraphicsPipelineSlot m_MarkerSlot{};
			GraphicsPipelineSlot m_GeometrySlot{};
			GraphicsPipelineSlot m_BackCullSlot{};
			GraphicsPipelineSlot m_DepthSlot{};
			GraphicsPipelineSlot m_PositionSlot{};
			bool m_Initialized = false;
		};
	}

	CoordinateConformanceLabSession::CoordinateConformanceLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, nullptr),
		m_State(std::make_shared<CoordinateConformanceLabState>())
	{
		SetRenderPipeline(std::make_unique<CoordinateConformanceRenderPipeline>(m_State));
	}

	void CoordinateConformanceLabSession::Update(float deltaTime) noexcept
	{
		GGLAB_UNUSED(deltaTime);
		GetCamera().Update();
	}

	void CoordinateConformanceLabSession::BuildDiagnostics(
		LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		diagnostics.m_Title = "Coordinate Conformance";
		const uint64_t markerExecutions =
			m_State->m_MarkerExecutions.load(std::memory_order_relaxed);
		const uint64_t conformanceExecutions =
			m_State->m_ConformanceExecutions.load(std::memory_order_relaxed);
		const bool rendered = markerExecutions != 0 && markerExecutions == conformanceExecutions;
		diagnostics.m_Metrics = {
			{ .m_Name = "Marker passes", .m_Value = std::to_string(markerExecutions) },
			{ .m_Name = "Conformance passes", .m_Value = std::to_string(conformanceExecutions) },
		};
		diagnostics.m_Checks.push_back({
			.m_Name = "Coordinate draw sequence",
			.m_Status = rendered ? LabDiagnosticCheckStatus::Passed
				: LabDiagnosticCheckStatus::Pending,
			.m_Detail = rendered
				? "Top-left shows both winding colors; bottom-right overlays back-face culling on "
					"the position gradient so only the front-facing triangle remains. Indexed "
					"sampling and reversed-Z probes also executed."
				: "Waiting for the first complete marker and conformance frame.",
		});
	}

	LabId CoordinateConformanceLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.coordinate_conformance");
	}

	LabDescriptor CoordinateConformanceLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Coordinate Conformance",
			.m_Category = "Rendering",
			.m_Description =
				"Validates winding, back-face culling, upper-left UVs, indexed drawing, scissor, SV_Position, fullscreen triangles, and reversed-Z depth.",
			.m_Kind = LabKind::Pipeline,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> CoordinateConformanceLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<CoordinateConformanceLabSession>(createInfo);
	}
}
