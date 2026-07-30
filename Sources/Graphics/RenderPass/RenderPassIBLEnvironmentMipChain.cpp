#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassIBLEnvironmentMipChain.h"
#include "Graphics/Renderer.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/RenderPass/IBLGraphResources.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/RHI/RHITextureViewDescUtils.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/Shader/ShaderManager.h"

namespace gglab
{
	namespace
	{
		struct IBLEnvironmentMipPassParameters
		{
			uint32_t CubemapFaceIndex = 0;
			uint32_t SourceTextureIndex = 0;
			uint32_t SourceSamplerIndex = 0;
			uint32_t Padding = 0;
		};
		static_assert(IsPassRootConstantStruct<IBLEnvironmentMipPassParameters>);
		static_assert(sizeof(IBLEnvironmentMipPassParameters) == 16);

		struct PassData
		{
			RGTextureId m_SourceEnvironmentCubemap{};
			RGTextureId m_EnvironmentCubemap{};
			RGTextureViewId m_SourceSrv{};
			std::array<RGTextureViewId, CubemapFaceCount> m_Rtvs{};

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_SourceSamplerIndex = 0;
			bool m_IsLastMip = false;
			RHIFormat m_RenderTargetFormat = RHIFormat::Unknown;
		};
	}

	void RenderPassIBLEnvironmentMipChain::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_UNUSED(context);

		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);
		auto* renderResRegistry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(renderResRegistry);

		auto* bakeScheduler = renderer->GetIBLBakeScheduler();
		GGLAB_ASSERT_NOT_NULL(bakeScheduler);
		const uint64_t bakeGeneration = bakeScheduler->GetBakingGeneration();

		const auto* textureDesc = renderResRegistry->GetIBLBakeTextureDesc(
			RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap);
		GGLAB_ASSERT_NOT_NULL(textureDesc);
		if (!textureDesc || textureDesc->m_MipLevels <= 1)
		{
			return;
		}

		EnsureInitialized(services);
		const uint32_t sourceSamplerIndex = renderer->GetSamplerRegistry()->GetSamplerIndex(
			SamplerPreset::LinearClamp);

		for (uint32_t mipLevel = 1; mipLevel < textureDesc->m_MipLevels; ++mipLevel)
		{
			const std::string passName = MakeRenderGraphPassName(std::to_string(mipLevel));
			rg.AddPass<PassData>(passName.c_str(),
				[renderResRegistry, mipLevel, sourceSamplerIndex](RenderGraph::RGBuilder& builder, PassData& data)
				{
					builder.SideEffect();

					auto& iblRes = builder.GetBlackboard().Get<RGIBLResources>(IBLResourcesName);
					const auto* desc = renderResRegistry->GetIBLBakeTextureDesc(
						RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap);
					GGLAB_ASSERT_NOT_NULL(desc);

					const RHISubresourceRange sourceRange{
						.m_BaseMip = mipLevel - 1,
						.m_MipCount = 1,
						.m_BaseArraySlice = 0,
						.m_ArraySliceCount = CubemapFaceCount,
						.m_Aspects = RHITextureAspect::Color,
					};
					data.m_SourceEnvironmentCubemap = builder.Read(
						iblRes.m_BakeEnvironmentCubemap,
						RGTextureAccess::Sample,
						sourceRange);

					const RHISubresourceRange targetRange{
						.m_BaseMip = mipLevel,
						.m_MipCount = 1,
						.m_BaseArraySlice = 0,
						.m_ArraySliceCount = CubemapFaceCount,
						.m_Aspects = RHITextureAspect::Color,
					};
					builder.WriteInPlace(
						iblRes.m_BakeEnvironmentCubemap,
						RGTextureAccess::RenderTarget,
						targetRange);
					data.m_EnvironmentCubemap = iblRes.m_BakeEnvironmentCubemap;

					RHITextureViewDesc sourceSrvDesc{};
					sourceSrvDesc.m_Type = RHITextureViewType::ShaderResource;
					sourceSrvDesc.m_Dimension = RHITextureViewDimension::TextureCube;
					sourceSrvDesc.m_Format = desc->m_Format;
					sourceSrvDesc.m_Subresources = sourceRange;
					data.m_SourceSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
						data.m_SourceEnvironmentCubemap,
						sourceSrvDesc);

					for (uint32_t face = 0; face < CubemapFaceCount; ++face)
					{
						const auto rtvDesc = MakeRHITexture2DArrayViewDesc(desc->m_Format, mipLevel, face, 1);
						data.m_Rtvs[face] = builder.CreateView<RHITextureViewType::RenderTarget>(
							data.m_EnvironmentCubemap,
							rtvDesc);
					}

					data.m_Width = std::max(1u, desc->m_Extent.m_Width >> mipLevel);
					data.m_Height = std::max(1u, desc->m_Extent.m_Height >> mipLevel);
					data.m_SourceSamplerIndex = sourceSamplerIndex;
					data.m_IsLastMip = mipLevel + 1u == desc->m_MipLevels;
					data.m_RenderTargetFormat = desc->m_Format;
				},
				[this, renderer, bakeScheduler, bakeGeneration](RGExecuteContext& executeContext, PassData& data)
				{
					auto* commandContext = executeContext.GetGraphicsCommandContext();
					commandContext->SetPipeline(GetOrCreatePSO(*renderer, data.m_RenderTargetFormat));
					commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width), static_cast<float>(data.m_Height) });
					commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width), static_cast<int32_t>(data.m_Height) });

					const auto sourceSrv = executeContext.GetViewDescriptor(data.m_SourceSrv);
					GGLAB_ASSERT_MSG(sourceSrv.IsValid(), "Environment mip source SRV must be shader visible.");
					for (uint32_t face = 0; face < CubemapFaceCount; ++face)
					{
						const auto rtv = executeContext.GetViewHandle(data.m_Rtvs[face]);
						commandContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&rtv, 1));
						commandContext->ClearColor(rtv, { 0.0f, 0.0f, 0.0f, 1.0f });

						const IBLEnvironmentMipPassParameters passParameters{
							.CubemapFaceIndex = face,
							.SourceTextureIndex = sourceSrv.m_Index,
							.SourceSamplerIndex = data.m_SourceSamplerIndex,
						};
						commandContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
							passParameters);
						commandContext->DrawFullscreenTriangle();
					}

					if (data.m_IsLastMip)
					{
						bakeScheduler->NotifyStageExecuted(
							IBLBakeStage::EnvironmentMipChain,
							bakeGeneration);
					}
				});
		}
	}

	void RenderPassIBLEnvironmentMipChain::EnsureInitialized(const RenderServices& services) noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}

		auto* renderer = services.m_Renderer;
		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(renderer);
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		ShaderDesc shaderDesc{};
		shaderDesc.m_SourcePath = L"Passes/PassIBLEnvironmentMip.hlsl";
		shaderDesc.m_Stage = ShaderStage::Vertex;
		shaderDesc.m_Entry = L"VSMain";
		const auto vsId = shaderManager->LoadShader(shaderDesc);

		shaderDesc.m_Stage = ShaderStage::Pixel;
		shaderDesc.m_Entry = L"PSMain";
		const auto psId = shaderManager->LoadShader(shaderDesc);

		m_BaseRecipe.m_BindingLayout = renderer->GetCommonBindingLayout();
		m_BaseRecipe.m_InputLayoutId = InputLayoutID::None;
		m_BaseRecipe.m_VSId = vsId;
		m_BaseRecipe.m_PSId = psId;
		m_BaseRecipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		m_BaseRecipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
		m_BaseRecipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R16G16B16A16Float;
		m_BaseRecipe.m_Formats.m_RenderTargetCount = 1;
		m_BaseRecipe.m_Formats.m_DepthStencilFormat = RHIFormat::Unknown;
		m_BaseRecipe.m_Formats.m_SampleCount = 1;
		m_BaseRecipe.m_Formats.m_SampleQuality = 0;
		m_BaseRecipe.m_RasterizerPreset = RasterizerPreset::Default;
		m_BaseRecipe.m_BlendPreset = BlendPreset::Default;
		m_BaseRecipe.m_DepthPreset = DepthPreset::DepthDisabled;
		m_IsInitialized = true;
	}

	RHIPipelineHandle RenderPassIBLEnvironmentMipChain::GetOrCreatePSO(
		const Renderer& renderer,
		RHIFormat renderTargetFormat) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		GraphicsPhysicalPipelineKey recipe = m_BaseRecipe;
		recipe.m_Formats.m_RenderTargetFormats[0] = renderTargetFormat;
		return pipelineCache->Resolve(m_PipelineSlot, recipe, GetInfo());
	}
}
