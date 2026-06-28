#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassShadowMapPreview.h"
#include "Graphics/Renderer.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGShadowResources.h"

namespace gglab
{
	namespace
	{
		struct ShadowMapPreviewPassParameters
		{
			uint32_t ShadowMapTextureIndex = 0;
			uint32_t ShadowMapSamplerIndex = 0;
			float PreviewMinDepth = 0.0f;
			float PreviewMaxDepth = 1.0f;
			uint32_t PreviewInvert = 0;
			uint32_t Padding[3]{};
		};
		static_assert(IsPassRootConstantStruct<ShadowMapPreviewPassParameters>);
		static_assert(sizeof(ShadowMapPreviewPassParameters) == 32);

		struct PassData
		{
			RGTextureId m_ShadowMap{};
			RGTextureId m_ShadowMapPreview{};
			RGTextureViewId m_ShadowMapSrv{};
			RGTextureViewId m_PreviewRtv{};

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_SamplerIndex = 0;
			float m_MinDepth = 0.0f;
			float m_MaxDepth = 1.0f;
			uint32_t m_Invert = 0;
		};
	}

	void RenderPassShadowMapPreview::AddPass(RenderGraph& rg,
		const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		auto* contextPtr = &context;
		GGLAB_ASSERT_NOT_NULL(contextPtr);

		auto* servicesPtr = &services;
		GGLAB_ASSERT_NOT_NULL(servicesPtr);

		EnsureInitialized(services);

		rg.AddPass<PassData>(GetRenderGraphPassName(),
			[contextPtr, servicesPtr](RenderGraph::RGBuilder& builder, PassData& data)
			{
				auto& shadowRes = builder.GetBlackboard().Get<RGShadowResources>(ShadowResourcesName);

				data.m_ShadowMap = builder.Read(shadowRes.m_DirectionalShadowMap, RGTextureAccess::Sample);
				data.m_ShadowMapPreview = builder.Write(
					shadowRes.m_DirectionalShadowMapPreview,
					RGTextureAccess::RenderTarget);

				const auto shadowMapSrvDesc = MakeRHITexture2DViewDesc(RHIFormat::R32Float, 0, 1);
				data.m_ShadowMapSrv =
					builder.CreateView<RHITextureViewType::ShaderResource>(data.m_ShadowMap, shadowMapSrvDesc);
				data.m_PreviewRtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_ShadowMapPreview);

				data.m_Width = shadowRes.m_ShadowMapPreviewSize;
				data.m_Height = shadowRes.m_ShadowMapPreviewSize;

				auto* renderer = servicesPtr->m_Renderer;
				GGLAB_ASSERT_NOT_NULL(renderer);
				data.m_SamplerIndex = renderer->GetSamplerRegistry()->GetSamplerIndex(SamplerPreset::PointClamp);

				const auto& settings = contextPtr->GetShadowVisualizationSettings();
				data.m_MinDepth = std::clamp(settings.m_PreviewMinDepth, 0.0f, 1.0f);
				data.m_MaxDepth = std::clamp(settings.m_PreviewMaxDepth, 0.0f, 1.0f);
				data.m_Invert = settings.m_PreviewInvert ? 1u : 0u;
			},
			[this, contextPtr, servicesPtr](RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto shadowMapSrv = executeContext.GetViewDescriptor(data.m_ShadowMapSrv);
				GGLAB_ASSERT_MSG(shadowMapSrv.IsValid(),
					"Shadow map preview SRV must expose a descriptor heap index.");

				const auto previewRtv = executeContext.GetViewHandle(data.m_PreviewRtv);
				commandContext->ClearColor(previewRtv, { 0.0f, 0.0f, 0.0f, 1.0f });

				auto* renderer = servicesPtr->m_Renderer;
				GGLAB_ASSERT_NOT_NULL(renderer);

				commandContext->SetPipeline(GetOrCreatePSO(*renderer));
				commandContext->SetRenderTargets(std::span<const RHITextureViewHandle>(&previewRtv, 1));
				commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width), static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width), static_cast<int32_t>(data.m_Height) });
				commandContext->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

				const auto* sceneBuffer = renderer->GetSceneConstantBuffer();
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					sceneBuffer->GetBufferHandle(),
					contextPtr->m_RenderScene.m_SceneConstantBufferOffset);

				const ShadowMapPreviewPassParameters passParameters{
					.ShadowMapTextureIndex = shadowMapSrv.m_Index,
					.ShadowMapSamplerIndex = data.m_SamplerIndex,
					.PreviewMinDepth = data.m_MinDepth,
					.PreviewMaxDepth = data.m_MaxDepth,
					.PreviewInvert = data.m_Invert,
				};
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
					passParameters);

				commandContext->Draw(3);
			});
	}

	void RenderPassShadowMapPreview::EnsureInitialized(const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		if (!m_IsInitialized)
		{
			ShaderDesc shaderDesc{};
			shaderDesc.m_SourcePath = L"Assets/Shaders/Passes/PassShadowMapPreview.hlsl";
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
			m_BaseRecipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R8G8B8A8Unorm;
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

	RHIPipelineHandle RenderPassShadowMapPreview::GetOrCreatePSO(const Renderer& renderer) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(m_PipelineSlot, m_BaseRecipe, GetInfo());
	}
}
