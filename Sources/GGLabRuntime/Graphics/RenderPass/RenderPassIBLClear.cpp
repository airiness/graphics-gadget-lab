#include "Graphics/RenderPass/RenderPassIBLClear.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPass/IBLGraphResources.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"

#include <span>
#include <string>
#include <vector>

namespace gglab
{
	namespace
	{
		struct PassData
		{
			std::vector<RGTextureViewId> m_Rtvs;
		};
	}

	void RenderPassIBLClear::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_UNUSED(context);
		auto* renderer = services.m_Renderer;
		auto* registry = renderer ? renderer->GetRenderResourceRegistry() : nullptr;
		GGLAB_ASSERT_NOT_NULL(registry);
		if (!registry || registry->HasInitializedActiveIBL())
		{
			return;
		}

		rg.AddPass<PassData>(
			GetRenderGraphPassName(),
			[registry](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();
				auto& resources = builder.GetBlackboard().Get<RGIBLResources>(IBLResourcesName);
				auto& previewResources =
					builder.GetBlackboard().Get<RGIBLPreviewResources>(IBLPreviewResourcesName);
				using TextureIndex = RenderResourceRegistry::TextureIndex;
				struct Target
				{
					TextureIndex m_Index;
					RGTextureId* m_Texture;
				};
				Target targets[] = {
					{TextureIndex::IBL_EnvironmentCubemap, &resources.m_EnvironmentCubemap},
					{TextureIndex::IBL_IrradianceCubemap, &resources.m_IrradianceCubemap},
					{TextureIndex::IBL_PrefilteredSpecularCubemap,
						&resources.m_PrefilteredSpecularCubemap},
					{TextureIndex::IBL_BrdfLut, &resources.m_BrdfLut},
					{TextureIndex::Preview_IBL_EnvironmentCubemap,
						&previewResources.m_EnvironmentCubemapPreview},
					{TextureIndex::Preview_IBL_IrradianceCubemap,
						&previewResources.m_IrradianceCubemapPreview},
					{TextureIndex::Preview_IBL_PrefilteredSpecularCubemap,
						&previewResources.m_PrefilteredSpecularCubemapPreview},
				};

				for (const Target& target : targets)
				{
					const auto* desc = registry->GetTextureDesc(target.m_Index);
					GGLAB_ASSERT_NOT_NULL(desc);
					builder.WriteInPlace(*target.m_Texture, RGTextureAccess::RenderTarget);
					for (uint32_t mip = 0; mip < desc->m_MipLevels; ++mip)
					{
						for (uint32_t slice = 0; slice < desc->m_ArraySize; ++slice)
						{
							const auto viewDesc =
								MakeRHITexture2DArrayViewDesc(desc->m_Format, mip, slice, 1);
							data.m_Rtvs.push_back(
								builder.CreateView<RHITextureViewType::RenderTarget>(
									*target.m_Texture, viewDesc));
						}
					}
				}
			},
			[registry](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				for (const RGTextureViewId viewId : data.m_Rtvs)
				{
					const auto rtv = executeContext.GetViewHandle(viewId);
					const RHIRenderingAttachment colorAttachment{
						.m_View = rtv,
						.m_LoadOp = RHIContentLoadOp::DontCare,
					};
					commandContext->BeginRendering({ .m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
					commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
					commandContext->EndRendering();
				}
				registry->MarkActiveIBLInitialized();
			});
	}

	void RenderPassIBLClear::AddBakePass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_UNUSED(context);
		auto* renderer = services.m_Renderer;
		auto* registry = renderer ? renderer->GetRenderResourceRegistry() : nullptr;
		auto* bakeScheduler = renderer ? renderer->GetIBLBakeScheduler() : nullptr;
		GGLAB_ASSERT_NOT_NULL(registry);
		GGLAB_ASSERT_NOT_NULL(bakeScheduler);
		if (!registry || !bakeScheduler || !bakeScheduler->ShouldInitializeBakeResources())
		{
			return;
		}

		const uint64_t bakeGeneration = bakeScheduler->GetBakingGeneration();
		const std::string passName = MakeRenderGraphPassName("BakeTargets");
		rg.AddPass<PassData>(
			passName.c_str(),
			[registry](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();
				auto& resources = builder.GetBlackboard().Get<RGIBLResources>(IBLResourcesName);
				using TextureIndex = RenderResourceRegistry::TextureIndex;
				struct Target
				{
					TextureIndex m_Index;
					RGTextureId* m_Texture;
				};
				Target targets[] = {
					{TextureIndex::IBL_EnvironmentCubemap, &resources.m_BakeEnvironmentCubemap},
					{TextureIndex::IBL_IrradianceCubemap, &resources.m_BakeIrradianceCubemap},
					{TextureIndex::IBL_PrefilteredSpecularCubemap,
						&resources.m_BakePrefilteredSpecularCubemap},
					{TextureIndex::IBL_BrdfLut, &resources.m_BakeBrdfLut},
				};

				for (const Target& target : targets)
				{
					const auto* desc = registry->GetIBLBakeTextureDesc(target.m_Index);
					GGLAB_ASSERT_NOT_NULL(desc);
					builder.WriteInPlace(*target.m_Texture, RGTextureAccess::RenderTarget);
					for (uint32_t mip = 0; mip < desc->m_MipLevels; ++mip)
					{
						for (uint32_t slice = 0; slice < desc->m_ArraySize; ++slice)
						{
							const auto viewDesc =
								MakeRHITexture2DArrayViewDesc(desc->m_Format, mip, slice, 1);
							data.m_Rtvs.push_back(
								builder.CreateView<RHITextureViewType::RenderTarget>(
									*target.m_Texture, viewDesc));
						}
					}
				}
			},
			[bakeScheduler, bakeGeneration](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				for (const RGTextureViewId viewId : data.m_Rtvs)
				{
					const auto rtv = executeContext.GetViewHandle(viewId);
					const RHIRenderingAttachment colorAttachment{
						.m_View = rtv,
						.m_LoadOp = RHIContentLoadOp::DontCare,
					};
					commandContext->BeginRendering({ .m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
					commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
					commandContext->EndRendering();
				}
				bakeScheduler->NotifyBakeResourcesInitialized(bakeGeneration);
			});
	}
}
