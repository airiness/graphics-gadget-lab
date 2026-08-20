#include "Application/Demo/DemoLoadingShellRenderPipeline.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/RenderPassIBL.h"
#include "Graphics/RenderPass/ShadowGraphResources.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RenderPipeline/RenderPipelineOverlayExtensionBase.h"
#include "Graphics/RenderGraph/RGResourceUtils.h"
#include "Graphics/Resource/RenderResourceRegistry.h"

namespace gglab
{
	namespace
	{
		struct LoadingShellSetupPassData
		{
			RGTextureId m_BackBuffer{};
			RGTextureViewId m_Rtv{};
			RGTextureViewId m_ShadowPreviewRtv{};
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
				const auto shadowIndex = RenderResourceRegistry::TextureIndex::
					Preview_Shadow_DirectionalShadowMap;
				const bool shadowPreviewInitialized = !resourceRegistry->IsDirty(shadowIndex);

				const uint32_t backBufferIndex = context.m_BackBufferIndex;
				const RenderViewID displayViewId = context.GetDisplayViewId();
				rg.AddPass<LoadingShellSetupPassData>(
					"LoadingShell.Setup",
					[swapChain, resourceRegistry, backBufferIndex, displayViewId, shadowIndex,
						shadowPreviewInitialized](
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
							swapChain->GetBackBufferInitialState(backBufferIndex),
							RGContentValidity::Undefined);
						builder.WriteInPlace(targets.m_BackBuffer, RGTextureAccess::RenderTarget);
						data.m_BackBuffer = targets.m_BackBuffer;
						data.m_Rtv =
							builder.CreateView<RHITextureViewType::RenderTarget>(data.m_BackBuffer);

						auto& shadow = builder.GetBlackboard().GetOrCreate<RGShadowResources>(
							ShadowResourcesName);
						const auto* shadowDesc = resourceRegistry->GetTextureDesc(shadowIndex);
						GGLAB_ASSERT_NOT_NULL(shadowDesc);
						const RGPersistentTextureImportContract shadowImport =
							ResolveRGPersistentTextureImportContract(shadowPreviewInitialized);
						shadow.m_DirectionalShadowMapPreview =
							builder.ImportTexture("LoadingShell.ShadowPreview",
								resourceRegistry->GetTextureHandle(shadowIndex), *shadowDesc,
								shadowImport.m_InitialState,
								shadowImport.m_InitialContentValidity);
						if (!shadowPreviewInitialized)
						{
							builder.WriteInPlace(
								shadow.m_DirectionalShadowMapPreview, RGTextureAccess::RenderTarget);
							data.m_ShadowPreviewRtv =
								builder.CreateView<RHITextureViewType::RenderTarget>(
									shadow.m_DirectionalShadowMapPreview);
						}
					},
					[renderer, resourceRegistry, shadowIndex](
						RGExecuteContext& executeContext, LoadingShellSetupPassData& data)
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
						commandContext->EndRendering();

						if (data.m_ShadowPreviewRtv.IsValid())
						{
							const auto shadowRtv =
								executeContext.GetViewHandle(data.m_ShadowPreviewRtv);
							const RHIRenderingAttachment shadowAttachment{
								.m_View = shadowRtv,
								.m_LoadOp = RHIContentLoadOp::DontCare,
							};
							commandContext->BeginRendering({ .m_ColorAttachments =
								std::span<const RHIRenderingAttachment>(&shadowAttachment, 1) });
							commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
							commandContext->EndRendering();
							resourceRegistry->ClearDirty(shadowIndex);
						}
					});

				m_IBLPass.AddPass(rg, context, services);
				if (services.m_OverlayExtension)
				{
					services.m_OverlayExtension->AddOverlayPasses(rg, context, services);
				}
				m_IBLPass.AddFinishPass(rg);

				rg.AddPass<LoadingShellFinishPassData>("LoadingShell.Finish",
					[displayViewId](
						RenderGraph::RGBuilder& builder, LoadingShellFinishPassData&)
					{
						builder.SideEffect();
						auto& targets = builder.GetBlackboard()
							.Get<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(displayViewId);
						auto& shadow = builder.GetBlackboard().Get<RGShadowResources>(
							ShadowResourcesName);
						builder.Export(
							shadow.m_DirectionalShadowMapPreview, RGTextureAccess::None);
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

	std::unique_ptr<RenderPipelineBase>
		CreateDemoLoadingShellRenderPipeline() noexcept
	{
		return std::make_unique<RenderPipelineLoadingShell>();
	}
}
