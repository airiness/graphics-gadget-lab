#include "Application/Demo/DemoLoadingShellRenderPipeline.h"
#include "Core/CoreMacros.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/RenderPassIBL.h"
#include "Graphics/RenderPass/ShadowGraphResources.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RenderPipeline/RenderPipelineOverlayExtensionBase.h"
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

		struct LoadingShellFinishPassData
		{
		};

		class RenderPipelineLoadingShell final : public RenderPipelineBase
		{
		public:
			std::string_view GetName() const noexcept override
			{
				return "RenderPipeline.LoadingShell";
			}

			void BuildRenderGraph(RenderGraph& rg, const RenderFrameContext& context,
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
						RenderGraph::RGBuilder& builder, LoadingShellSetupPassData& data)
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
						targets.m_BackBuffer = builder.ImportTexture("LoadingShell.BackBuffer",
							swapChain->GetBackBufferHandle(backBufferIndex), backBufferDesc,
							RGTextureAccess::Present, RGContentValidity::Undefined);
						builder.WriteInPlace(targets.m_BackBuffer, RGTextureAccess::RenderTarget);
						data.m_BackBuffer = targets.m_BackBuffer;
						data.m_Rtv =
							builder.CreateView<RHITextureViewType::RenderTarget>(data.m_BackBuffer);

						auto& shadow = builder.GetBlackboard().GetOrCreate<RGShadowResources>(
							ShadowResourcesName);
						const auto shadowIndex = RenderResourceRegistry::TextureIndex::
							Preview_Shadow_DirectionalShadowMap;
						const auto* shadowDesc = resourceRegistry->GetTextureDesc(shadowIndex);
						GGLAB_ASSERT_NOT_NULL(shadowDesc);
						shadow.m_DirectionalShadowMapPreview =
							builder.ImportTexture("LoadingShell.ShadowPreview",
								resourceRegistry->GetTextureHandle(shadowIndex), *shadowDesc,
								RGTextureAccess::None, RGContentValidity::Defined);
					},
					[renderer](RGExecuteContext& executeContext, LoadingShellSetupPassData& data)
					{
						auto* commandContext = executeContext.GetGraphicsCommandContext();
						const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
						const RHIRenderingAttachment colorAttachment{
							.m_View = rtv,
							.m_LoadOp = RHIContentLoadOp::DontCare,
						};
						commandContext->BeginRendering({ .m_ColorAttachments =
							std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
						commandContext->ClearColorAttachment(0, renderer->GetBackBufferClearColor());
					});

				m_IBLPass.AddPass(rg, context, services);
				if (services.m_OverlayExtension)
				{
					services.m_OverlayExtension->AddOverlayPasses(rg, context, services);
				}
				m_IBLPass.AddFinishPass(rg);

				rg.AddPass<LoadingShellFinishPassData>("LoadingShell.Finish",
					[displayViewId](RenderGraph::RGBuilder& builder, LoadingShellFinishPassData&)
					{
						builder.SideEffect();
						auto& targets = builder.GetBlackboard()
							.Get<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						auto& shadow =
							builder.GetBlackboard().Get<RGShadowResources>(ShadowResourcesName);
						builder.Export(shadow.m_DirectionalShadowMapPreview, RGTextureAccess::None);
						builder.Export(targets.m_BackBuffer, RGTextureAccess::Present,
							RHISubresourceRange{
								.m_MipCount = 1,
								.m_ArraySliceCount = 1,
								.m_Aspects = RHITextureAspect::Color,
							});
					});
			}

		private:
			RenderPassIBL m_IBLPass;
		};
	}

	std::unique_ptr<RenderPipelineBase> CreateDemoLoadingShellRenderPipeline() noexcept
	{
		return std::make_unique<RenderPipelineLoadingShell>();
	}
}
