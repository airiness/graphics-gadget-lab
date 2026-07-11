#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassIBLClear.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPass/IBLGraphResources.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/RHI/RHITextureViewDescUtils.h"

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
		RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_UNUSED(context);
		auto* renderer = services.m_Renderer;
		auto* registry = renderer ? renderer->GetRenderResourceRegistry() : nullptr;
		GGLAB_ASSERT_NOT_NULL(registry);
		if (!registry || registry->HasInitializedActiveIBL())
		{
			return;
		}

		rg.AddPass<PassData>(GetRenderGraphPassName(),
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
					{ TextureIndex::IBL_EnvironmentCubemap, &resources.m_EnvironmentCubemap },
					{ TextureIndex::IBL_IrradianceCubemap, &resources.m_IrradianceCubemap },
					{ TextureIndex::IBL_PrefilteredSpecularCubemap, &resources.m_PrefilteredSpecularCubemap },
					{ TextureIndex::IBL_BrdfLut, &resources.m_BrdfLut },
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
							const auto viewDesc = MakeRHITexture2DArrayViewDesc(desc->m_Format, mip, slice, 1);
							data.m_Rtvs.push_back(builder.CreateView<RHITextureViewType::RenderTarget>(
								*target.m_Texture,
								viewDesc));
						}
					}
				}
			},
			[registry](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				for (const RGTextureViewId viewId : data.m_Rtvs)
				{
					commandContext->ClearColor(
						executeContext.GetViewHandle(viewId),
						{ 0.0f, 0.0f, 0.0f, 1.0f });
				}
				registry->MarkActiveIBLInitialized();
			});
	}
}
