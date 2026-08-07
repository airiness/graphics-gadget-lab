#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassIBLPreview.h"
#include "Graphics/Renderer.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/IBLGraphResources.h"
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

		struct CubemapPreviewPassData
		{
			RGTextureId m_SourceCubemap{};
			RGTextureId m_PreviewTexture{};
			RGTextureViewId m_Rtv{};

			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_DisplayLayout = 0;
			uint32_t m_CubemapTextureIndex = 0;
			uint32_t m_CubemapSamplerIndex = 0;
			uint32_t m_SampleMip = 0;
		};
	}

	void RenderPassIBLPreview::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);

		auto* renderResRegistry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(renderResRegistry);
		EnsureInitialized(services);

		using TextureIndex = RenderResourceRegistry::TextureIndex;
		using PreviewType = RenderResourceRegistry::IBLPreviewType;
		using PreviewLayout = RenderResourceRegistry::IBLPreviewLayout;
		const auto* contextPtr = &context;

		auto addPreviewPass = [this, &rg, renderer, renderResRegistry, contextPtr](
			const char* suffix, PreviewType previewType,
			TextureIndex sourceIndex, TextureIndex previewIndex,
			PreviewLayout layout, uint32_t sampleMip) noexcept
			{
				const std::string passName = MakeRenderGraphPassName(suffix);
				rg.AddPass<CubemapPreviewPassData>(
					passName.c_str(),
					[renderer, renderResRegistry, previewType, sourceIndex, previewIndex, layout,
					sampleMip](RenderGraph::RGBuilder& builder, CubemapPreviewPassData& data)
					{
						builder.SideEffect();
						auto& blackboard = builder.GetBlackboard();
						auto& iblResources = blackboard.Get<RGIBLResources>(IBLResourcesName);
						auto& previewResources =
							blackboard.Get<RGIBLPreviewResources>(IBLPreviewResourcesName);

						RGTextureId source{};
						RGTextureId* preview = nullptr;
						switch (previewType)
						{
						case PreviewType::Environment:
							source = iblResources.m_EnvironmentCubemap;
							preview = &previewResources.m_EnvironmentCubemapPreview;
							break;
						case PreviewType::Irradiance:
							source = iblResources.m_IrradianceCubemap;
							preview = &previewResources.m_IrradianceCubemapPreview;
							break;
						case PreviewType::PrefilteredSpecular:
							source = iblResources.m_PrefilteredSpecularCubemap;
							preview = &previewResources.m_PrefilteredSpecularCubemapPreview;
							break;
						default:
							GGLAB_ASSERT_MSG(false, "Unknown IBL preview type.");
							return;
						}

						data.m_SourceCubemap = builder.Read(source, RGTextureAccess::Sample);
						builder.WriteInPlace(*preview, RGTextureAccess::RenderTarget);
						data.m_PreviewTexture = *preview;

						const auto* previewDesc = renderResRegistry->GetTextureDesc(previewIndex);
						GGLAB_ASSERT_NOT_NULL(previewDesc);
						data.m_Rtv =
							builder.CreateView<RHITextureViewType::RenderTarget>(data.m_PreviewTexture);
						data.m_Width = static_cast<uint32_t>(previewDesc->m_Extent.m_Width);
						data.m_Height = previewDesc->m_Extent.m_Height;
						data.m_DisplayLayout = static_cast<uint32_t>(layout);
						data.m_CubemapTextureIndex =
							renderResRegistry->GetShaderVisibleSrvIndex(sourceIndex);
						data.m_CubemapSamplerIndex =
							renderer->GetSamplerRegistry()->GetSamplerIndex(SamplerPreset::LinearClamp);
						data.m_SampleMip = sampleMip;
					},
					[this, renderer, renderResRegistry, contextPtr, previewType](
						RGExecuteContext& executeContext, CubemapPreviewPassData& data)
					{
						auto* commandContext = executeContext.GetGraphicsCommandContext();
						const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
						const RHIRenderingAttachment colorAttachment{
							.m_View = rtv,
							.m_LoadOp = RHIContentLoadOp::DontCare,
						};
						commandContext->BeginRendering({ .m_ColorAttachments =
							std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
						commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
						commandContext->SetPipeline(GetOrCreateCubemapPreviewPSO(*renderer));
						commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width),
							static_cast<float>(data.m_Height) });
						commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width),
							static_cast<int32_t>(data.m_Height) });
						const auto* sceneBuffer = renderer->GetSceneConstantBuffer();
						commandContext->SetConstantBuffer(
							static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
							sceneBuffer->GetBufferHandle(),
							contextPtr->m_RenderScene.m_SceneConstantBufferOffset);

						const IBLCubemapPreviewPassParameters passParameters{
							.DisplayLayout = data.m_DisplayLayout,
							.CubemapTextureIndex = data.m_CubemapTextureIndex,
							.CubemapSamplerIndex = data.m_CubemapSamplerIndex,
							.SampleMip = data.m_SampleMip,
						};
						commandContext->SetPushConstants(
							static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants),
							passParameters);

						commandContext->DrawFullscreenTriangle();
						renderResRegistry->ClearIBLPreviewDirty(previewType);
					});
			};

		if (renderResRegistry->ConsumeIBLPreviewRequest(PreviewType::Environment))
		{
			const auto* environmentDesc =
				renderResRegistry->GetTextureDesc(TextureIndex::IBL_EnvironmentCubemap);
			const uint32_t mipLevels = environmentDesc ? environmentDesc->m_MipLevels : 0;
			const uint32_t sampleMip =
				mipLevels > 0
				? std::min(renderResRegistry->GetIBLEnvironmentPreviewMip(), mipLevels - 1u)
				: 0u;
			addPreviewPass("EnvironmentCubemap", PreviewType::Environment,
				TextureIndex::IBL_EnvironmentCubemap, TextureIndex::Preview_IBL_EnvironmentCubemap,
				renderResRegistry->GetIBLEnvironmentPreviewLayout(), sampleMip);
		}

		if (renderResRegistry->ConsumeIBLPreviewRequest(PreviewType::Irradiance))
		{
			addPreviewPass("IrradianceCubemap", PreviewType::Irradiance,
				TextureIndex::IBL_IrradianceCubemap, TextureIndex::Preview_IBL_IrradianceCubemap,
				renderResRegistry->GetIBLIrradiancePreviewLayout(), 0);
		}

		if (renderResRegistry->ConsumeIBLPreviewRequest(PreviewType::PrefilteredSpecular))
		{
			const auto* prefilteredDesc =
				renderResRegistry->GetTextureDesc(TextureIndex::IBL_PrefilteredSpecularCubemap);
			const uint32_t mipLevels = prefilteredDesc ? prefilteredDesc->m_MipLevels : 0;
			const uint32_t sampleMip =
				mipLevels > 0 ? std::min(renderResRegistry->GetIBLPrefilteredSpecularPreviewMip(),
					mipLevels - 1u)
				: 0u;
			addPreviewPass("PrefilteredSpecularCubemap", PreviewType::PrefilteredSpecular,
				TextureIndex::IBL_PrefilteredSpecularCubemap,
				TextureIndex::Preview_IBL_PrefilteredSpecularCubemap,
				renderResRegistry->GetIBLPrefilteredSpecularPreviewLayout(), sampleMip);
		}
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
			shaderDesc.m_SourcePath = L"Passes/PassIBLCubemapPreview.hlsl";
			shaderDesc.m_Stage = ShaderStage::Vertex;
			shaderDesc.m_Entry = L"VSMain";
			const auto vsId = shaderManager->LoadShader(shaderDesc);

			shaderDesc.m_Stage = ShaderStage::Pixel;
			shaderDesc.m_Entry = L"PSMain";
			const auto psId = shaderManager->LoadShader(shaderDesc);

			m_CubemapPreviewRecipe.m_BindingLayout = renderer->GetCommonBindingLayout();
			m_CubemapPreviewRecipe.m_InputLayoutId = InputLayoutID::None;
			m_CubemapPreviewRecipe.m_VSId = vsId;
			m_CubemapPreviewRecipe.m_PSId = psId;

			m_CubemapPreviewRecipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
			m_CubemapPreviewRecipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
			m_CubemapPreviewRecipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R8G8B8A8Unorm;
			m_CubemapPreviewRecipe.m_Formats.m_RenderTargetCount = 1;
			m_CubemapPreviewRecipe.m_Formats.m_DepthStencilFormat = RHIFormat::Unknown;
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
			m_CubemapPreviewPipelineSlot, m_CubemapPreviewRecipe, GetInfo());
	}
}
