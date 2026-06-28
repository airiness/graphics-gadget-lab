#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassIBLIrradiance.h"
#include "Graphics/Renderer.h"
#include "Graphics/ShaderManager.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGIBLResources.h"
#include "Graphics/SamplerRegistry.h"

namespace gglab
{
	namespace
	{
		struct IBLIrradiancePassParameters
		{
			uint32_t CubemapFaceIndex = 0;
			uint32_t EnvironmentTextureIndex = 0;
			uint32_t EnvironmentSamplerIndex = 0;
			uint32_t Padding = 0;
		};
		static_assert(IsPassRootConstantStruct<IBLIrradiancePassParameters>);
		static_assert(sizeof(IBLIrradiancePassParameters) == 16);

		struct PassData
		{
			RGTextureId m_EnvironmentCubemap{};
			RGTextureId m_IrradianceCubemap{};
			std::array<RGTextureViewId, CubemapFaceCount> m_Rtvs{};

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_EnvironmentTextureIndex = 0;
			uint32_t m_EnvironmentSamplerIndex = 0;
		};
	}

	void RenderPassIBLIrradiance::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		GGLAB_UNUSED(context);

		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* renderResRegistry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(renderResRegistry);

		const auto shouldBuild = renderResRegistry->IsDirty(
			RenderResourceRegistry::TextureIndex::IBL_IrradianceCubemap);

		if (!shouldBuild)
		{
			return;
		}

		EnsureInitialized(services);

		rg.AddPass<PassData>(GetRenderGraphPassName(),
			[renderer, renderResRegistry](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);

				data.m_EnvironmentCubemap = builder.Read(iblRes.m_EnvironmentCubemap, RGTextureAccess::Sample);
				data.m_IrradianceCubemap = builder.Write(iblRes.m_IrradianceCubemap, RGTextureAccess::RenderTarget);

				const auto* textureDesc = renderResRegistry->GetTextureDesc(
					RenderResourceRegistry::TextureIndex::IBL_IrradianceCubemap);
				GGLAB_ASSERT_NOT_NULL(textureDesc);

				for (uint32_t face = 0; face < CubemapFaceCount; ++face)
				{
					const auto rtvDesc = MakeRHITexture2DArrayViewDesc(textureDesc->m_Format, 0, face, 1);
					data.m_Rtvs[face] =
						builder.CreateView<RHITextureViewType::RenderTarget>(data.m_IrradianceCubemap, rtvDesc);
				}

				data.m_Width = textureDesc->m_Extent.m_Width;
				data.m_Height = textureDesc->m_Extent.m_Height;
				data.m_EnvironmentTextureIndex = renderResRegistry->GetShaderVisibleSrvIndex(
					RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap);
				data.m_EnvironmentSamplerIndex = renderer->GetSamplerRegistry()->GetSamplerIndex(
					SamplerPreset::LinearClamp);
			},
			[this, renderer, renderResRegistry](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				commandContext->SetPipeline(GetOrCreatePSO(*renderer));
				commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width), static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width), static_cast<int32_t>(data.m_Height) });
				commandContext->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

				for (uint32_t face = 0; face < CubemapFaceCount; ++face)
				{
					const auto rtv = executeContext.GetViewHandle(data.m_Rtvs[face]);
					commandContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&rtv, 1));
					commandContext->ClearColor(rtv, { 0.0f, 0.0f, 0.0f, 1.0f });

					const IBLIrradiancePassParameters passParameters{
						.CubemapFaceIndex = face,
						.EnvironmentTextureIndex = data.m_EnvironmentTextureIndex,
						.EnvironmentSamplerIndex = data.m_EnvironmentSamplerIndex,
					};
					commandContext->SetPushConstants(
						static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
						passParameters);

					commandContext->Draw(3);
				}

				renderResRegistry->ClearDirty(RenderResourceRegistry::TextureIndex::IBL_IrradianceCubemap);
			});
	}

	void RenderPassIBLIrradiance::EnsureInitialized(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassIBLIrradiance.hlsl";
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

	RHIPipelineHandle RenderPassIBLIrradiance::GetOrCreatePSO(const Renderer& renderer) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(m_PipelineSlot, m_BaseRecipe, GetInfo());
	}
}
