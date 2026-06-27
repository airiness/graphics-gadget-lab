#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassIBLPreview.h"
#include "Graphics/Renderer.h"
#include "Graphics/ShaderManager.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGIBLResources.h"
#include "Graphics/RenderGraph/RGIBLPreviewResources.h"
#include "Graphics/RenderGraph/RGDX12ResourceUtils.h"
#include "Graphics/SamplerRegistry.h"

#include <algorithm>

namespace gglab
{
	namespace
	{
		struct IBLCubemapPreviewPassParameters
		{
			uint32_t DisplayLayout = 0;
			uint32_t CubemapTextureIndex = 0;
			uint32_t CubemapSamplerIndex = 0;
			uint32_t SampleMip = 0;
		};
		static_assert(IsPassRootConstantStruct<IBLCubemapPreviewPassParameters>);
		static_assert(sizeof(IBLCubemapPreviewPassParameters) == 16);

		struct EnvironmentPreviewPassData
		{
			RGTextureId m_EnvironmentCubemap{};
			RGTextureId m_EnvironmentCubemapPreview{};
			RGTextureViewId m_Rtv{};

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_DisplayLayout = 0;
			uint32_t m_EnvironmentTextureIndex = 0;
			uint32_t m_EnvironmentSamplerIndex = 0;
			uint32_t m_SampleMip = 0;
		};

		struct PrefilteredSpecularPreviewPassData
		{
			RGTextureId m_PrefilteredSpecularCubemap{};
			RGTextureId m_PrefilteredSpecularCubemapPreview{};
			RGTextureViewId m_Rtv{};

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_DisplayLayout = 0;
			uint32_t m_PrefilteredSpecularTextureIndex = 0;
			uint32_t m_PrefilteredSpecularSamplerIndex = 0;
			uint32_t m_SampleMip = 0;
		};
	}

	void RenderPassIBLPreview::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* renderResRegistry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(renderResRegistry);
		renderResRegistry->EnsureIblResources();

		EnsureInitialized(services);

		auto* contextPtr = &context;
		rg.AddPass<EnvironmentPreviewPassData>("RenderPassIBLPreview.EnvironmentCubemap",
			[renderer, renderResRegistry](RenderGraph::RGBuilder& builder, EnvironmentPreviewPassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);
				auto& previewRes = blackboard.GetOrCreate<RGIBLPreviewResources>(IBLPreviewResourcesName);

				data.m_EnvironmentCubemap = builder.Read(iblRes.m_EnvironmentCubemap, RGTextureUsage::Sample);

				auto* previewTexture = renderResRegistry->GetTexture(
					RenderResourceRegistry::TextureIndex::Preview_IBL_EnvironmentCubemap);
				GGLAB_ASSERT_NOT_NULL(previewTexture);

				const auto previewDesc = ToRGTextureDesc(*previewTexture,
					RGTextureUsage::RenderTarget | RGTextureUsage::Sample);

				previewRes.m_EnvironmentCubemapPreview = builder.ImportTexture(
					"Preview.IBL.EnvironmentCubemap",
					renderResRegistry->GetTextureHandle(
						RenderResourceRegistry::TextureIndex::Preview_IBL_EnvironmentCubemap),
					previewDesc,
					RGTextureUsage::None);

				data.m_EnvironmentCubemapPreview = builder.Write(
					previewRes.m_EnvironmentCubemapPreview,
					RGTextureUsage::RenderTarget);

				data.m_Rtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_EnvironmentCubemapPreview);
				data.m_Width = previewDesc.m_Width;
				data.m_Height = previewDesc.m_Height;
				data.m_DisplayLayout = static_cast<uint32_t>(renderResRegistry->GetIBLEnvironmentPreviewLayout());
				data.m_EnvironmentTextureIndex = renderResRegistry->GetShaderVisibleSrvIndex(
					RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap);
				data.m_EnvironmentSamplerIndex = renderer->GetSamplerRegistry()->GetSamplerIndex(
					SamplerPreset::LinearClamp);
				data.m_SampleMip = 0;
			},
			[this, renderer, contextPtr](RGExecuteContext& executeContext, EnvironmentPreviewPassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
				commandContext->ClearColor(rtv, { 0.0f, 0.0f, 0.0f, 1.0f });
				commandContext->SetPipeline(GetOrCreateCubemapPreviewPSO(*renderer));
				commandContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&rtv, 1));
				commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width), static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width), static_cast<int32_t>(data.m_Height) });
				commandContext->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
				const auto* sceneBuffer = renderer->GetSceneConstantBuffer();
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					sceneBuffer->GetBufferHandle(),
					sceneBuffer->GetFrameOffset(contextPtr->m_BackBufferIndex));

				const IBLCubemapPreviewPassParameters passParameters{
					.DisplayLayout = data.m_DisplayLayout,
					.CubemapTextureIndex = data.m_EnvironmentTextureIndex,
					.CubemapSamplerIndex = data.m_EnvironmentSamplerIndex,
					.SampleMip = data.m_SampleMip,
				};
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
					passParameters);

				commandContext->Draw(3);

			});

		rg.AddPass<PrefilteredSpecularPreviewPassData>("RenderPassIBLPreview.PrefilteredSpecularCubemap",
			[renderer, renderResRegistry](RenderGraph::RGBuilder& builder, PrefilteredSpecularPreviewPassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);
				auto& previewRes = blackboard.GetOrCreate<RGIBLPreviewResources>(IBLPreviewResourcesName);

				data.m_PrefilteredSpecularCubemap = builder.Read(
					iblRes.m_PrefilteredSpecularCubemap,
					RGTextureUsage::Sample);

				auto* previewTexture = renderResRegistry->GetTexture(
					RenderResourceRegistry::TextureIndex::Preview_IBL_PrefilteredSpecularCubemap);
				GGLAB_ASSERT_NOT_NULL(previewTexture);

				const auto previewDesc = ToRGTextureDesc(*previewTexture,
					RGTextureUsage::RenderTarget | RGTextureUsage::Sample);

				previewRes.m_PrefilteredSpecularCubemapPreview = builder.ImportTexture(
					"Preview.IBL.PrefilteredSpecularCubemap",
					renderResRegistry->GetTextureHandle(
						RenderResourceRegistry::TextureIndex::Preview_IBL_PrefilteredSpecularCubemap),
					previewDesc,
					RGTextureUsage::None);

				data.m_PrefilteredSpecularCubemapPreview = builder.Write(
					previewRes.m_PrefilteredSpecularCubemapPreview,
					RGTextureUsage::RenderTarget);

				data.m_Rtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_PrefilteredSpecularCubemapPreview);
				data.m_Width = previewDesc.m_Width;
				data.m_Height = previewDesc.m_Height;
				data.m_DisplayLayout = static_cast<uint32_t>(renderResRegistry->GetIBLPrefilteredSpecularPreviewLayout());
				data.m_PrefilteredSpecularTextureIndex = renderResRegistry->GetShaderVisibleSrvIndex(
					RenderResourceRegistry::TextureIndex::IBL_PrefilteredSpecularCubemap);
				data.m_PrefilteredSpecularSamplerIndex = renderer->GetSamplerRegistry()->GetSamplerIndex(
					SamplerPreset::LinearClamp);

				auto* prefilteredSpecularTexture = renderResRegistry->GetTexture(
					RenderResourceRegistry::TextureIndex::IBL_PrefilteredSpecularCubemap);
				GGLAB_ASSERT_NOT_NULL(prefilteredSpecularTexture);
				const auto prefilteredSpecularDesc = prefilteredSpecularTexture->GetDesc();
				const uint32_t mipLevels = static_cast<uint32_t>(prefilteredSpecularDesc.MipLevels);
				data.m_SampleMip = mipLevels > 0 ?
					std::min(renderResRegistry->GetIBLPrefilteredSpecularPreviewMip(), mipLevels - 1u) :
					0u;
			},
			[this, renderer, contextPtr](RGExecuteContext& executeContext, PrefilteredSpecularPreviewPassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
				commandContext->ClearColor(rtv, { 0.0f, 0.0f, 0.0f, 1.0f });
				commandContext->SetPipeline(GetOrCreateCubemapPreviewPSO(*renderer));
				commandContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&rtv, 1));
				commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width), static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width), static_cast<int32_t>(data.m_Height) });
				commandContext->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
				const auto* sceneBuffer = renderer->GetSceneConstantBuffer();
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					sceneBuffer->GetBufferHandle(),
					sceneBuffer->GetFrameOffset(contextPtr->m_BackBufferIndex));

				const IBLCubemapPreviewPassParameters passParameters{
					.DisplayLayout = data.m_DisplayLayout,
					.CubemapTextureIndex = data.m_PrefilteredSpecularTextureIndex,
					.CubemapSamplerIndex = data.m_PrefilteredSpecularSamplerIndex,
					.SampleMip = data.m_SampleMip,
				};
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
					passParameters);

				commandContext->Draw(3);
			});
	}

	void RenderPassIBLPreview::EnsureInitialized(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassIBLCubemapPreview.hlsl";
			shaderDesc.m_Stage = ShaderStage::Vertex;
			shaderDesc.m_Entry = L"VSMain";
			const auto vsId = shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_Stage = ShaderStage::Pixel;
			shaderDesc.m_Entry = L"PSMain";
			const auto psId = shaderManager->LoadShader(shaderDesc);

			m_CubemapPreviewRecipe.m_RootSignatureId = renderer->GetCommonRootSignatureId();
			m_CubemapPreviewRecipe.m_InputLayoutId = InputLayoutID::None;
			m_CubemapPreviewRecipe.m_VSId = vsId;
			m_CubemapPreviewRecipe.m_PSId = psId;

			m_CubemapPreviewRecipe.m_Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			m_CubemapPreviewRecipe.m_Formats.m_RtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			m_CubemapPreviewRecipe.m_Formats.m_RtvFormats.NumRenderTargets = 1;
			m_CubemapPreviewRecipe.m_Formats.m_DsvFormat = DXGI_FORMAT_UNKNOWN;
			m_CubemapPreviewRecipe.m_Formats.m_SampleCount = 1;
			m_CubemapPreviewRecipe.m_Formats.m_SampleQuality = 0;

			m_CubemapPreviewRecipe.m_RasterizerPreset = RasterizerPreset::Default;
			m_CubemapPreviewRecipe.m_BlendPreset = BlendPreset::Default;
			m_CubemapPreviewRecipe.m_DepthPreset = DepthPreset::DepthDisabled;

			m_IsInitialized = true;
		}

	}

	RHIPipelineHandle RenderPassIBLPreview::GetOrCreateCubemapPreviewPSO(
		const Renderer& renderer) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(
			m_CubemapPreviewPipelineSlot,
			m_CubemapPreviewRecipe);
	}
}
