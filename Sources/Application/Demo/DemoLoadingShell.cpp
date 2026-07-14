#include "Core/Precompiled.h"
#include "Application/Demo/DemoLoadingShell.h"
#include "Graphics/Camera.h"
#include "Graphics/CameraController.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/RenderPassDevelopGui.h"
#include "Graphics/RenderPass/RenderPassIBL.h"
#include "Graphics/RenderPass/ShadowGraphResources.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/Resource/RenderResourceRegistry.h"

namespace gglab
{
	namespace
	{
		struct LoadingShellSetupPassData
		{
			RGTextureId m_BackBuffer{};
			RGTextureViewId m_Rtv{};
		};

		struct LoadingShellFinishPassData {};

		class RenderPipelineLoadingShell final : public RenderPipelineBase
		{
		public:
			std::string_view GetName() const noexcept override
			{
				return "RenderPipeline.LoadingShell";
			}

			void BuildRenderGraph(
				RenderGraph& rg,
				const RenderFrameContext& context,
				const RenderServices& services) noexcept override
			{
				auto* renderer = services.m_Renderer;
				GGLAB_ASSERT_NOT_NULL(renderer);
				auto* swapChain = renderer->GetSwapChain();
				auto* resourceRegistry = renderer->GetRenderResourceRegistry();
				GGLAB_ASSERT_NOT_NULL(swapChain);
				GGLAB_ASSERT_NOT_NULL(resourceRegistry);
				resourceRegistry->EnsureShadowPreviewResources();

				const uint32_t backBufferIndex = context.m_BackBufferIndex;
				const RenderViewID displayViewId = context.GetDisplayViewId();
				rg.AddPass<LoadingShellSetupPassData>(
					"LoadingShell.Setup",
					[swapChain, resourceRegistry, backBufferIndex, displayViewId](
						RenderGraph::RGBuilder& builder,
						LoadingShellSetupPassData& data)
					{
						builder.SideEffect();
						auto& targets = builder.GetBlackboard()
							.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						targets.m_Width = swapChain->GetBufferWidth();
						targets.m_Height = swapChain->GetBufferHeight();

						RHITextureDesc backBufferDesc{};
						backBufferDesc.m_Extent = {
							swapChain->GetBufferWidth(),
							swapChain->GetBufferHeight(),
							1u,
						};
						backBufferDesc.m_Format = swapChain->GetFormat();
						targets.m_BackBuffer = builder.ImportTexture(
							"LoadingShell.BackBuffer",
							swapChain->GetBackBufferHandle(backBufferIndex),
							backBufferDesc,
							RGTextureAccess::Present);
						builder.WriteInPlace(targets.m_BackBuffer, RGTextureAccess::RenderTarget);
						data.m_BackBuffer = targets.m_BackBuffer;
						data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(data.m_BackBuffer);

						auto& shadow = builder.GetBlackboard()
							.GetOrCreate<RGShadowResources>(ShadowResourcesName);
						const auto shadowIndex = RenderResourceRegistry::TextureIndex::
							Preview_Shadow_DirectionalShadowMap;
						const auto* shadowDesc = resourceRegistry->GetTextureDesc(shadowIndex);
						GGLAB_ASSERT_NOT_NULL(shadowDesc);
						shadow.m_DirectionalShadowMapPreview = builder.ImportTexture(
							"LoadingShell.ShadowPreview",
							resourceRegistry->GetTextureHandle(shadowIndex),
							*shadowDesc,
							RGTextureAccess::None);
					},
					[renderer](RGExecuteContext& executeContext, LoadingShellSetupPassData& data)
					{
						executeContext.GetGraphicsCommandContext()->ClearColor(
							executeContext.GetViewHandle(data.m_Rtv),
							renderer->GetBackBufferClearColor());
					});

				m_IBLPass.AddPass(rg, context, services);
				m_DevelopGuiPass.AddPass(rg, context, services);
				m_IBLPass.AddFinishPass(rg);

				rg.AddPass<LoadingShellFinishPassData>(
					"LoadingShell.Finish",
					[displayViewId](RenderGraph::RGBuilder& builder, LoadingShellFinishPassData&)
					{
						builder.SideEffect();
						auto& targets = builder.GetBlackboard()
							.Get<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						auto& shadow = builder.GetBlackboard()
							.Get<RGShadowResources>(ShadowResourcesName);
						builder.Export(
							shadow.m_DirectionalShadowMapPreview,
							RGTextureAccess::None);
						builder.Export(
							targets.m_BackBuffer,
							RGTextureAccess::Present,
							RHISubresourceRange{
								.m_MipCount = 1,
								.m_ArraySliceCount = 1,
								.m_Aspects = RHITextureAspect::Color,
							});
					});
			}

		private:
			RenderPassIBL m_IBLPass;
			RenderPassDevelopGui m_DevelopGuiPass;
		};
	}

	DemoLoadingShell::DemoLoadingShell(const DemoCreateInfo& createInfo) noexcept
	{
		GGLAB_ASSERT_MSG(createInfo.IsValid(), "DemoLoadingShell requires valid create info.");

		Camera::CreateInfo cameraCreateInfo{};
		cameraCreateInfo.m_Position = Vector3(0.0f, 0.0f, -5.0f);
		cameraCreateInfo.m_Width = createInfo.m_WindowWidth;
		cameraCreateInfo.m_Height = createInfo.m_WindowHeight;
		cameraCreateInfo.m_Near = 0.1f;
		cameraCreateInfo.m_Far = 1000.0f;
		cameraCreateInfo.m_Fov = 60.0f;
		m_Camera = std::make_unique<Camera>(cameraCreateInfo);

		m_CameraController = std::make_unique<CameraController>(CameraController::CreateInfo{});
		m_CameraRig.AttachMainCamera(*m_Camera, *m_CameraController);
		m_RenderPipeline = std::make_unique<RenderPipelineLoadingShell>();
	}

	void DemoLoadingShell::OnResize(uint32_t width, uint32_t height) noexcept
	{
		m_CameraRig.OnResize(width, height);
	}

	void DemoLoadingShell::Update() noexcept
	{
		m_CameraRig.GetActiveCamera().Update();
	}
}
