#include "Graphics/RenderPass/RenderPassSkybox.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/IBLGraphResources.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/Shader/ShaderProgramCatalog.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"

#include <cstdint>
#include <span>

namespace gglab
{
	namespace
	{
		struct SkyboxPassParameters
		{
			uint32_t ViewIndex = 0;
			uint32_t EnvironmentTextureIndex = 0;
			uint32_t EnvironmentSamplerIndex = 0;
			uint32_t Padding = 0;
		};
		static_assert(IsPassRootConstantStruct<SkyboxPassParameters>);
		static_assert(sizeof(SkyboxPassParameters) == 16);

		struct PassData
		{
			RGTextureId m_EnvironmentCubemap{};
			RGTextureId m_SceneColor{};
			RGTextureId m_Depth{};
			RGTextureViewId m_Rtv{};
			RGTextureViewId m_Dsv{};
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_EnvironmentTextureIndex = 0;
			uint32_t m_EnvironmentSamplerIndex = 0;
		};
	}

	void RenderPassSkybox::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		if (!context.IsRenderSceneReady())
		{
			return;
		}

		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);
		auto* assetManager = services.m_AssetManager;
		GGLAB_ASSERT_NOT_NULL(assetManager);
		const auto* environmentSystem = renderer->GetEnvironmentLightingSystem();
		if (!environmentSystem || !environmentSystem->GetSettings().m_EnableSkybox)
		{
			return;
		}

		EnsureInitialized(services);
		auto* bakeScheduler = renderer->GetIBLBakeScheduler();
		auto* renderResourceRegistry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(bakeScheduler);
		GGLAB_ASSERT_NOT_NULL(renderResourceRegistry);

		const bool useFallback = bakeScheduler->GetStatus().m_ActiveGeneration == 0;
		RHITextureHandle fallbackTextureHandle{};
		RHITextureDesc fallbackTextureDesc{};
		uint32_t environmentTextureIndex = 0;
		if (useFallback)
		{
			const TextureID fallbackId =
				ToTextureId(ReservedTextureIDIndex::FallbackEnvironmentCubemap);
			const TextureContentRef fallbackContent =
				assetManager->GetTextureContentRef(fallbackId);
			const auto fallbackResource = assetManager->GetResidentTextureResource(fallbackContent);
			GGLAB_ASSERT_MSG(fallbackResource.has_value(),
				"Skybox fallback cubemap must be ready before the first frame.");
			if (!fallbackResource)
			{
				return;
			}
			assetManager->MarkTextureUsed(fallbackContent.m_Id);
			fallbackTextureHandle = fallbackResource->m_Texture;
			fallbackTextureDesc = fallbackResource->m_Desc;
			environmentTextureIndex = fallbackResource->m_SrvIndex;
		}
		else
		{
			environmentTextureIndex = renderResourceRegistry->GetShaderVisibleSrvIndex(
				RenderResourceRegistry::TextureIndex::IBL_EnvironmentCubemap);
		}
		const uint32_t environmentSamplerIndex =
			renderer->GetSamplerRegistry()->GetSamplerIndex(SamplerPreset::LinearClamp);
		const RenderViewID displayViewId = context.GetDisplayViewId();
		const auto* contextPtr = &context;

		rg.AddPass<PassData>(
			GetRenderGraphPassName(),
			[displayViewId, useFallback, fallbackTextureHandle, fallbackTextureDesc,
			environmentTextureIndex,
			environmentSamplerIndex](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				if (useFallback)
				{
					data.m_EnvironmentCubemap =
						builder.ImportTexture("Skybox.FallbackEnvironmentCubemap",
							fallbackTextureHandle, fallbackTextureDesc, RGTextureAccess::Sample,
							RGContentValidity::Defined);
					data.m_EnvironmentCubemap =
						builder.Read(data.m_EnvironmentCubemap, RGTextureAccess::Sample);
				}
				else
				{
					auto& iblResources = blackboard.Get<RGIBLResources>(IBLResourcesName);
					data.m_EnvironmentCubemap =
						builder.Read(iblResources.m_EnvironmentCubemap, RGTextureAccess::Sample);
				}
				auto& targets = blackboard.Get<RGViewTargetsTable>(ViewTargetsTableName)
					.GetViewTargets(displayViewId);
				auto& sceneDepth = blackboard.Get<RGSceneDepthResources>(SceneDepthResourcesName);
				builder.WriteInPlace(targets.m_SceneColor, RGTextureAccess::RenderTarget);
				data.m_SceneColor = targets.m_SceneColor;
				data.m_Rtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_SceneColor);
				data.m_Depth =
					builder.Read(sceneDepth.m_Texture, RGTextureAccess::DepthStencilRead);
				RHITextureViewDesc readOnlyDsvDesc = sceneDepth.m_DsvDesc;
				readOnlyDsvDesc.m_ReadOnlyDepth = true;
				data.m_Dsv = builder.CreateView<RHITextureViewType::DepthStencil>(
					data.m_Depth, readOnlyDsvDesc);
				data.m_Width = targets.m_Width;
				data.m_Height = targets.m_Height;
				const RHITextureDesc& colorDesc = builder.GetTextureDesc(data.m_SceneColor);
				const RHITextureDesc& depthDesc = builder.GetTextureDesc(data.m_Depth);
				GGLAB_ASSERT_MSG(colorDesc.m_Extent.m_Width == depthDesc.m_Extent.m_Width &&
					colorDesc.m_Extent.m_Height == depthDesc.m_Extent.m_Height &&
					colorDesc.m_Extent.m_Depth == depthDesc.m_Extent.m_Depth &&
					colorDesc.m_SampleCount == depthDesc.m_SampleCount,
					"Skybox color and depth targets must have matching extents and sample counts.");
				data.m_EnvironmentTextureIndex = environmentTextureIndex;
				data.m_EnvironmentSamplerIndex = environmentSamplerIndex;
			},
			[this, renderer, contextPtr, displayViewId](
				RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto rtv = executeContext.GetViewHandle(data.m_Rtv);
				const auto dsv = executeContext.GetViewHandle(data.m_Dsv);

				commandContext->SetPipeline(GetOrCreatePSO(*renderer));
				const RHIRenderingAttachment colorAttachment{ .m_View = rtv };
				commandContext->BeginRendering({
					.m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(&colorAttachment, 1),
					.m_DepthAttachment = RHIRenderingAttachment{ .m_View = dsv },
				});
				commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width),
					static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width),
					static_cast<int32_t>(data.m_Height) });

				const auto* sceneBuffer = renderer->GetSceneConstantBuffer();
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					sceneBuffer->GetBufferHandle(),
					contextPtr->m_RenderScene.m_SceneConstantBufferOffset);

				const auto* viewBuffer = renderer->GetViewStructuredBuffer();
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					viewBuffer->GetBufferHandle());

				const SkyboxPassParameters passParameters{
					.ViewIndex = static_cast<uint32_t>(utils::ToIndex(displayViewId)),
					.EnvironmentTextureIndex = data.m_EnvironmentTextureIndex,
					.EnvironmentSamplerIndex = data.m_EnvironmentSamplerIndex,
				};
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), passParameters);
				commandContext->DrawFullscreenTriangle();
			});
	}

	void RenderPassSkybox::EnsureInitialized(const RenderServices& services) noexcept
	{
		if (m_IsInitialized)
		{
			return;
		}

		auto* renderer = services.m_Renderer;
		auto* shaderManager = services.m_ShaderManager;
		GGLAB_ASSERT_NOT_NULL(renderer);
		GGLAB_ASSERT_NOT_NULL(shaderManager);

		const auto vsId = shaderManager->LoadProgram(shader_programs::SkyboxVertex);
		const auto psId = shaderManager->LoadProgram(shader_programs::SkyboxPixel);

		m_BaseRecipe.m_BindingLayout = renderer->GetCommonBindingLayout();
		m_BaseRecipe.m_InputLayoutId = InputLayoutID::None;
		m_BaseRecipe.m_VSId = vsId;
		m_BaseRecipe.m_PSId = psId;
		m_BaseRecipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		m_BaseRecipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
		m_BaseRecipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R16G16B16A16Float;
		m_BaseRecipe.m_Formats.m_RenderTargetCount = 1;
		m_BaseRecipe.m_Formats.m_DepthStencilFormat = RHIFormat::D32Float;
		m_BaseRecipe.m_Formats.m_SampleCount = 1;
		m_BaseRecipe.m_Formats.m_SampleQuality = 0;
		m_BaseRecipe.m_RasterizerPreset = RasterizerPreset::Default;
		m_BaseRecipe.m_BlendPreset = BlendPreset::Default;
		m_BaseRecipe.m_DepthPreset = DepthPreset::ReversedZEqualReadOnly;
		m_IsInitialized = true;
	}

	RHIPipelineHandle RenderPassSkybox::GetOrCreatePSO(const Renderer& renderer) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(m_PipelineSlot, m_BaseRecipe, GetInfo());
	}
}
