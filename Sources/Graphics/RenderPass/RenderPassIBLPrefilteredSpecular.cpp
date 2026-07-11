#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassIBLPrefilteredSpecular.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/Renderer.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/IBLGraphResources.h"
#include "Graphics/RHI/RHITextureViewDescUtils.h"
#include "Graphics/SamplerRegistry.h"

namespace gglab
{
	namespace
	{
		struct IBLPrefilteredSpecularPassParameters
		{
			uint32_t CubemapFaceIndex = 0;
			uint32_t MipLevel = 0;
			uint32_t MipLevels = 0;
			uint32_t EnvironmentTextureIndex = 0;
			uint32_t EnvironmentSamplerIndex = 0;
			uint32_t EnvironmentResolution = 0;
			uint32_t EnvironmentMipLevels = 0;
			uint32_t SampleCount = 0;
			float MaxSampleLuminance = 0.0f;
			uint32_t Padding[3]{};
		};
		static_assert(IsPassRootConstantStruct<IBLPrefilteredSpecularPassParameters>);
		static_assert(sizeof(IBLPrefilteredSpecularPassParameters) == 48);

		struct PassData
		{
			RGTextureId m_EnvironmentCubemap{};
			RGTextureId m_PrefilteredSpecularCubemap{};
			std::vector<RGTextureViewId> m_Rtvs;

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_MipLevels = 0;
			uint32_t m_EnvironmentTextureIndex = 0;
			uint32_t m_EnvironmentSamplerIndex = 0;
			uint32_t m_EnvironmentResolution = 0;
			uint32_t m_EnvironmentMipLevels = 0;
			uint32_t m_SampleCount = 0;
			float m_MaxSampleLuminance = 0.0f;
			RHIFormat m_RenderTargetFormat = RHIFormat::Unknown;
		};
	}

	void RenderPassIBLPrefilteredSpecular::AddPass(RenderGraph& rg,
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

		EnsureInitialized(services);

		const auto& config = bakeScheduler->GetBakingConfig();
		const uint32_t sampleCount = config.m_PrefilteredSpecularSampleCount;
		const float maxSampleLuminance = config.m_PrefilteredSpecularMaxSampleLuminance;
		rg.AddPass<PassData>(GetRenderGraphPassName(),
			[renderer, renderResRegistry, sampleCount, maxSampleLuminance](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);

				data.m_EnvironmentCubemap = builder.Read(iblRes.m_BakeEnvironmentCubemap, RGTextureAccess::Sample);
				builder.WriteInPlace(
					iblRes.m_BakePrefilteredSpecularCubemap,
					RGTextureAccess::RenderTarget);
				data.m_PrefilteredSpecularCubemap = iblRes.m_BakePrefilteredSpecularCubemap;

				const auto* textureDesc = renderResRegistry->GetIBLBakeTextureDesc(
					RenderResourceRegistry::TextureIndex::IBL_PrefilteredSpecularCubemap);
				GGLAB_ASSERT_NOT_NULL(textureDesc);

				data.m_Rtvs.resize(textureDesc->m_MipLevels * CubemapFaceCount);
				for (uint32_t mip = 0; mip < textureDesc->m_MipLevels; ++mip)
				{
					for (uint32_t face = 0; face < CubemapFaceCount; ++face)
					{
						const auto rtvDesc = MakeRHITexture2DArrayViewDesc(textureDesc->m_Format, mip, face, 1);
						const auto rtvIndex = mip * CubemapFaceCount + face;

						data.m_Rtvs[rtvIndex] =
							builder.CreateView<RHITextureViewType::RenderTarget>(
								data.m_PrefilteredSpecularCubemap,
								rtvDesc);
					}
				}

				data.m_Width = textureDesc->m_Extent.m_Width;
				data.m_Height = textureDesc->m_Extent.m_Height;
				data.m_MipLevels = textureDesc->m_MipLevels;
				data.m_EnvironmentTextureIndex = renderResRegistry->GetIBLBakeShaderVisibleSrvIndex(
					RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap);
				data.m_EnvironmentSamplerIndex = renderer->GetSamplerRegistry()->GetSamplerIndex(
					SamplerPreset::LinearClamp);

				const auto* environmentDesc = renderResRegistry->GetIBLBakeTextureDesc(
					RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap);
				GGLAB_ASSERT_NOT_NULL(environmentDesc);
				data.m_EnvironmentResolution = static_cast<uint32_t>(environmentDesc->m_Extent.m_Width);
				data.m_EnvironmentMipLevels = environmentDesc->m_MipLevels;
				data.m_SampleCount = sampleCount;
				data.m_MaxSampleLuminance = maxSampleLuminance;
				data.m_RenderTargetFormat = textureDesc->m_Format;
			},
			[this, renderer, bakeScheduler, bakeGeneration](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				commandContext->SetPipeline(GetOrCreatePSO(*renderer, data.m_RenderTargetFormat));
				commandContext->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

				for (uint32_t mip = 0; mip < data.m_MipLevels; ++mip)
				{
					const uint32_t mipWidth = std::max(1u, data.m_Width >> mip);
					const uint32_t mipHeight = std::max(1u, data.m_Height >> mip);

					commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(mipWidth), static_cast<float>(mipHeight) });
					commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(mipWidth), static_cast<int32_t>(mipHeight) });

					for (uint32_t face = 0; face < CubemapFaceCount; ++face)
					{
						const auto rtvIndex = mip * CubemapFaceCount + face;
						const auto rtv = executeContext.GetViewHandle(data.m_Rtvs[rtvIndex]);
						commandContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&rtv, 1));
						commandContext->ClearColor(rtv, { 0.0f, 0.0f, 0.0f, 1.0f });

						const IBLPrefilteredSpecularPassParameters passParameters{
							.CubemapFaceIndex = face,
							.MipLevel = mip,
							.MipLevels = data.m_MipLevels,
							.EnvironmentTextureIndex = data.m_EnvironmentTextureIndex,
							.EnvironmentSamplerIndex = data.m_EnvironmentSamplerIndex,
							.EnvironmentResolution = data.m_EnvironmentResolution,
							.EnvironmentMipLevels = data.m_EnvironmentMipLevels,
							.SampleCount = data.m_SampleCount,
							.MaxSampleLuminance = data.m_MaxSampleLuminance,
						};
						commandContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
							passParameters);

						commandContext->Draw(3);
					}
				}

				bakeScheduler->NotifyStageExecuted(IBLBakeStage::PrefilteredSpecular, bakeGeneration);
			});
	}

	void RenderPassIBLPrefilteredSpecular::EnsureInitialized(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassIBLPrefilteredSpecular.hlsl";
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

	}

	RHIPipelineHandle RenderPassIBLPrefilteredSpecular::GetOrCreatePSO(
		const Renderer& renderer,
		RHIFormat renderTargetFormat) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		GraphicsPipelineRecipe recipe = m_BaseRecipe;
		recipe.m_Formats.m_RenderTargetFormats[0] = renderTargetFormat;
		return pipelineCache->Resolve(m_PipelineSlot, recipe, GetInfo());
	}
}
