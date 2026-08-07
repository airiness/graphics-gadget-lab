#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassPostProcessPreview.h"
#include "Graphics/PostProcess/PostProcessGraphResources.h"
#include "Graphics/RenderPass/GTAOGraphResources.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/Renderer.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/Shader/ShaderManager.h"

namespace gglab
{
	namespace
	{
		struct PostProcessPreviewPassParameters
		{
			uint32_t SourceTextureIndex = 0;
			uint32_t SourceSamplerIndex = 0;
			uint32_t ViewIndex = 0;
			uint32_t SourceMode = 0;
			float SourcePreExposure = 1.0f;
			float PreviewExposureScale = 1.0f;
			float Padding1 = 0.0f;
			float Padding2 = 0.0f;
		};
		static_assert(IsPassRootConstantStruct<PostProcessPreviewPassParameters>);
		static_assert(sizeof(PostProcessPreviewPassParameters) == 32);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::SceneDepthRaw) == 4);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::SceneDepthLinearViewZ) == 5);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAORawAO) == 6);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAOHalfDepthViewZ) == 7);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAOReconstructedNormal) == 8);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAOSelectedSurfaceOffset) == 9);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAODenoiseX) == 10);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAODenoiseY) == 11);
		static_assert(static_cast<uint32_t>(PostProcessDebugTap::GTAOFinalAO) == 12);
		static_assert(static_cast<uint32_t>(
			PostProcessDebugTap::GTAOAOOnlyLightingContribution) == 13);

		struct PassData
		{
			RGTextureId m_Source{};
			RGTextureId m_Output{};
			RGTextureViewId m_SourceSrv{};
			RGTextureViewId m_OutputRtv{};
			PostProcessDebugSelection m_Selection{};
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_SamplerIndex = 0;
			float m_SourcePreExposure = 1.0f;
			float m_PreviewExposureScale = 1.0f;
		};

		RGPostProcessColor ResolvePreviewSource(
			const RGPostProcessResources& resources, PostProcessDebugSelection selection) noexcept
		{
			switch (selection.m_Tap)
			{
			case PostProcessDebugTap::SceneColor:
				return resources.m_Inputs.m_SceneColor;
			case PostProcessDebugTap::BloomResult:
				return resources.m_Bloom.m_Result;
			default:
				return {};
			}
		}

		bool IsDepthPreview(PostProcessDebugTap tap) noexcept
		{
			return tap == PostProcessDebugTap::SceneDepthRaw ||
				tap == PostProcessDebugTap::SceneDepthLinearViewZ;
		}

		bool IsGTAOPreview(PostProcessDebugTap tap) noexcept
		{
			return tap == PostProcessDebugTap::GTAORawAO ||
				tap == PostProcessDebugTap::GTAOHalfDepthViewZ ||
				tap == PostProcessDebugTap::GTAOReconstructedNormal ||
				tap == PostProcessDebugTap::GTAOSelectedSurfaceOffset ||
				tap == PostProcessDebugTap::GTAODenoiseX ||
				tap == PostProcessDebugTap::GTAODenoiseY ||
				tap == PostProcessDebugTap::GTAOFinalAO ||
				tap == PostProcessDebugTap::GTAOAOOnlyLightingContribution;
		}

		RGTextureId ResolveGTAOPreviewSource(
			const RGGTAOResources& resources, PostProcessDebugTap tap) noexcept
		{
			switch (tap)
			{
			case PostProcessDebugTap::GTAORawAO:
				return resources.m_RawAO;
			case PostProcessDebugTap::GTAOHalfDepthViewZ:
				return resources.m_HalfDepthViewZ;
			case PostProcessDebugTap::GTAOReconstructedNormal:
				return resources.m_ReconstructedNormal;
			case PostProcessDebugTap::GTAOSelectedSurfaceOffset:
				return resources.m_SelectedSurfaceOffset;
			case PostProcessDebugTap::GTAODenoiseX:
				return resources.m_DenoiseX;
			case PostProcessDebugTap::GTAODenoiseY:
				return resources.m_DenoiseY;
			case PostProcessDebugTap::GTAOFinalAO:
				return resources.m_FinalAO;
			case PostProcessDebugTap::GTAOAOOnlyLightingContribution:
				return resources.m_AOOnlyLightingContribution;
			default:
				return {};
			}
		}
	}

	void RenderPassPostProcessPreview::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);
		auto* registry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(registry);
		const auto selection = registry->GetPostProcessPreviewSelection();
		if (IsGTAOPreview(selection.m_Tap))
		{
			const auto& gtaoResources =
				rg.GetBlackboard().Get<RGGTAOResources>(GTAOResourcesName);
			const RGTextureId source = ResolveGTAOPreviewSource(gtaoResources, selection.m_Tap);
			if (!source.IsValid())
			{
				registry->InvalidatePostProcessPreview(selection);
				return;
			}
			if (!registry->ConsumePostProcessPreviewRequest())
			{
				return;
			}
			AddResolvedPass(rg, context, services, source, 1.0f, std::nullopt, selection);
			return;
		}
		if (IsDepthPreview(selection.m_Tap))
		{
			if (!registry->ConsumePostProcessPreviewRequest())
			{
				return;
			}
			const auto& sceneDepth =
				rg.GetBlackboard().Get<RGSceneDepthResources>(SceneDepthResourcesName);
			AddResolvedPass(
				rg, context, services, sceneDepth.m_Texture, 1.0f, sceneDepth.m_SrvDesc, selection);
			return;
		}
		if (selection.m_Tap != PostProcessDebugTap::SceneColor &&
			selection.m_Tap != PostProcessDebugTap::BloomResult)
		{
			return;
		}

		const auto& resources =
			rg.GetBlackboard().Get<RGPostProcessResources>(PostProcessResourcesName);
		const RGPostProcessColor source = ResolvePreviewSource(resources, selection);
		AddPassForTap(
			rg, context, services, source, selection.m_Tap, selection.m_BloomPyramidLevel);
	}

	void RenderPassPostProcessPreview::AddPassForTap(RenderGraph& rg,
		const RenderFrameContext& context, const RenderServices& services,
		const RGPostProcessColor& source, PostProcessDebugTap tap,
		uint32_t bloomPyramidLevel) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);
		auto* registry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(registry);
		if (!registry->IsPostProcessPreviewRequested())
		{
			return;
		}

		const auto selection = registry->GetPostProcessPreviewSelection();
		const bool levelMatches = tap != PostProcessDebugTap::BloomPyramid ||
			selection.m_BloomPyramidLevel == bloomPyramidLevel;
		if (selection.m_Tap != tap || !levelMatches || !source.m_Texture.IsValid())
		{
			return;
		}
		GGLAB_ASSERT_MSG(source.m_State == PostProcessColorState::SceneLinearRec709,
			"Post-process preview requires scene-linear Rec.709 input.");
		if (!registry->ConsumePostProcessPreviewRequest())
		{
			return;
		}

		AddResolvedPass(
			rg, context, services, source.m_Texture, source.m_PreExposure, std::nullopt, selection);
	}

	void RenderPassPostProcessPreview::AddResolvedPass(RenderGraph& rg,
		const RenderFrameContext& context, const RenderServices& services, RGTextureId source,
		float sourcePreExposure, std::optional<RHITextureViewDesc> sourceViewDesc,
		PostProcessDebugSelection selection) noexcept
	{
		auto* renderer = services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(renderer);
		auto* registry = renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(registry);
		if (!source.IsValid())
		{
			return;
		}
		GGLAB_ASSERT_MSG(
			sourcePreExposure > 0.0f, "Post-process preview requires positive pre-exposure.");

		EnsureInitialized(services);
		const RenderViewID displayViewId = context.GetDisplayViewId();
		const RenderView& displayView = context.GetDisplayRenderView();
		const RHIFencePoint retireFence = renderer->GetLastSubmittedFencePoint();
		registry->EnsurePostProcessPreviewResources(displayView.m_Width, displayView.m_Height,
			retireFence.IsValid() ? &retireFence : nullptr);

		using TextureIndex = RenderResourceRegistry::TextureIndex;
		constexpr TextureIndex PreviewIndex = TextureIndex::Preview_PostProcess;
		const auto* outputDesc = registry->GetTextureDesc(PreviewIndex);
		GGLAB_ASSERT_NOT_NULL(outputDesc);
		const RGTextureAccess initialAccess = registry->HasPublishedPostProcessPreview()
			? RGTextureAccess::Sample
			: RGTextureAccess::None;
		const bool pointSampledPreview =
			IsDepthPreview(selection.m_Tap) || IsGTAOPreview(selection.m_Tap);
		const uint32_t samplerIndex = renderer->GetSamplerRegistry()->GetSamplerIndex(
			pointSampledPreview ? SamplerPreset::PointClamp : SamplerPreset::LinearClamp);
		const float previewExposureScale = std::exp2(registry->GetPostProcessPreviewExposureEV());
		const auto* contextPtr = &context;

		rg.AddPass<PassData>(
			GetRenderGraphPassName(),
			[source, sourcePreExposure, sourceViewDesc, selection, outputDesc, initialAccess,
			samplerIndex, previewExposureScale,
			registry](RenderGraph::RGBuilder& builder, PassData& data)
			{
				data.m_Source = builder.Read(source, RGTextureAccess::Sample);
				data.m_Output = builder.ImportTexture("PostProcess.Preview.SelectedTap",
					registry->GetTextureHandle(TextureIndex::Preview_PostProcess), *outputDesc,
					initialAccess);
				builder.WriteInPlace(data.m_Output, RGTextureAccess::RenderTarget);
				if (!sourceViewDesc)
				{
					data.m_SourceSrv =
						builder.CreateView<RHITextureViewType::ShaderResource>(data.m_Source);
				}
				else
				{
					data.m_SourceSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
						data.m_Source, *sourceViewDesc);
				}
				data.m_OutputRtv =
					builder.CreateView<RHITextureViewType::RenderTarget>(data.m_Output);
				builder.Export(data.m_Output, RGTextureAccess::Sample, RHIStage::PixelShader);
				data.m_Selection = selection;
				data.m_Width = static_cast<uint32_t>(outputDesc->m_Extent.m_Width);
				data.m_Height = outputDesc->m_Extent.m_Height;
				data.m_SamplerIndex = samplerIndex;
				data.m_SourcePreExposure = sourcePreExposure;
				data.m_PreviewExposureScale = previewExposureScale;
			},
			[this, renderer, registry, contextPtr, displayViewId](
				RGExecuteContext& executeContext, PassData& data)
			{
				auto* commandContext = executeContext.GetGraphicsCommandContext();
				const auto sourceSrv = executeContext.GetViewDescriptor(data.m_SourceSrv);
				const auto outputRtv = executeContext.GetViewHandle(data.m_OutputRtv);
				GGLAB_ASSERT_MSG(
					sourceSrv.IsValid(), "Post-process preview source SRV must be shader visible.");

				const RHIRenderingAttachment colorAttachment{
					.m_View = outputRtv,
					.m_LoadOp = RHIContentLoadOp::DontCare,
				};
				commandContext->BeginRendering({ .m_ColorAttachments =
					std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
				commandContext->ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
				commandContext->SetPipeline(GetOrCreatePSO(*renderer));
				commandContext->SetViewport({ 0.0f, 0.0f, static_cast<float>(data.m_Width),
					static_cast<float>(data.m_Height) });
				commandContext->SetScissorRect({ 0, 0, static_cast<int32_t>(data.m_Width),
					static_cast<int32_t>(data.m_Height) });

				const auto* sceneBuffer = renderer->GetSceneConstantBuffer();
				commandContext->SetConstantBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
					sceneBuffer->GetBufferHandle(),
					contextPtr->m_RenderScene.m_SceneConstantBufferOffset);
				commandContext->SetReadOnlyBuffer(
					static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
					renderer->GetViewStructuredBuffer()->GetBufferHandle());

				const PostProcessPreviewPassParameters parameters{
					.SourceTextureIndex = sourceSrv.m_Index,
					.SourceSamplerIndex = data.m_SamplerIndex,
					.ViewIndex = static_cast<uint32_t>(utils::ToIndex(displayViewId)),
					.SourceMode = static_cast<uint32_t>(data.m_Selection.m_Tap),
					.SourcePreExposure = data.m_SourcePreExposure,
					.PreviewExposureScale = data.m_PreviewExposureScale,
				};
				commandContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
				commandContext->DrawFullscreenTriangle();
				registry->PublishPostProcessPreview(data.m_Selection);
			});
	}

	void RenderPassPostProcessPreview::EnsureInitialized(const RenderServices& services) noexcept
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
		shaderDesc.m_SourcePath = L"Passes/PassPostProcessPreview.hlsl";
		shaderDesc.m_Stage = ShaderStage::Vertex;
		shaderDesc.m_Entry = L"VSMain";
		m_BaseRecipe.m_VSId = shaderManager->LoadShader(shaderDesc);
		shaderDesc.m_Stage = ShaderStage::Pixel;
		shaderDesc.m_Entry = L"PSMain";
		m_BaseRecipe.m_PSId = shaderManager->LoadShader(shaderDesc);

		m_BaseRecipe.m_BindingLayout = renderer->GetCommonBindingLayout();
		m_BaseRecipe.m_InputLayoutId = InputLayoutID::None;
		m_BaseRecipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		m_BaseRecipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
		m_BaseRecipe.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R8G8B8A8Unorm;
		m_BaseRecipe.m_Formats.m_RenderTargetCount = 1;
		m_BaseRecipe.m_Formats.m_DepthStencilFormat = RHIFormat::Unknown;
		m_BaseRecipe.m_Formats.m_SampleCount = 1;
		m_BaseRecipe.m_RasterizerPreset = RasterizerPreset::Default;
		m_BaseRecipe.m_BlendPreset = BlendPreset::Default;
		m_BaseRecipe.m_DepthPreset = DepthPreset::DepthDisabled;
		m_IsInitialized = true;
	}

	RHIPipelineHandle RenderPassPostProcessPreview::GetOrCreatePSO(
		const Renderer& renderer) noexcept
	{
		auto* pipelineCache = renderer.GetPipelineCache();
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		return pipelineCache->Resolve(m_PipelineSlot, m_BaseRecipe, GetInfo());
	}
}
