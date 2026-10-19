#include "Graphics/RenderPass/ForwardShadingCommon.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "Graphics/RenderPass/AtmosphereGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/IBLGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/ShadowGraphResources.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"
#include "Graphics/SamplerRegistry.h"

namespace gglab::forward_shading
{
	ResolvedRenderLayout CreateBindingLayout(
		const RenderServices& services, bool forwardPlusLightLists) noexcept
	{
		const auto* common = services.m_BindingLayout->GetCommonLayoutContract();
		GGLAB_ASSERT_MSG(common, "Forward shading requires the common logical layout contract.");
		if (!common)
		{
			return {};
		}
		const auto composition = scene_bindings::ComposeForwardLayout(*common, forwardPlusLightLists);
		GGLAB_ASSERT_MSG(composition.IsSuccess(), "Forward shading layout fragments must compose.");
		if (!composition.m_Layout)
		{
			return {};
		}
		auto* rhiContext = services.m_Presentation->GetRHIContext();
		GGLAB_ASSERT_NOT_NULL(rhiContext);
		auto layout = ResolvedRenderLayout::Create(rhiContext->GetPipelineSystem(),
			*composition.m_Layout, forwardPlusLightLists ?
				"ForwardPBR.ForwardPlusBindingLayout" :
				"ForwardPBR.BindingLayout");
		GGLAB_ASSERT_MSG(layout.IsValid(), "Forward shading requires its pass-specific binding layout.");
		return layout;
	}

	GraphicsPhysicalPipelineKey MakeBasePhysicalKey(RHIBindingLayoutHandle bindingLayout,
		ShaderID vertexShader, ShaderID pixelShader) noexcept
	{
		GraphicsPhysicalPipelineKey key{};
		key.m_BindingLayout = bindingLayout;
		key.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
		key.m_VSId = vertexShader;
		key.m_PSId = pixelShader;
		key.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		key.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
		key.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R16G16B16A16Float;
		key.m_Formats.m_RenderTargetCount = 1;
		key.m_Formats.m_DepthStencilFormat = RHIFormat::D32Float;
		key.m_Formats.m_SampleCount = 1;
		key.m_Formats.m_SampleQuality = 0;
		key.m_RasterizerPreset = RasterizerPreset::Default;
		key.m_BlendPreset = BlendPreset::Default;
		key.m_DepthPreset = DepthPreset::ReversedZWrite;
		return key;
	}

	void AppendMaterialDiagnosticTargets(GraphicsPhysicalPipelineKey& key) noexcept
	{
		auto& formats = key.m_Formats;
		formats.m_RenderTargetFormats[formats.m_RenderTargetCount++] = RHIFormat::R16G16B16A16Float;
		formats.m_RenderTargetFormats[formats.m_RenderTargetCount++] = RHIFormat::R16Float;
		formats.m_RenderTargetFormats[formats.m_RenderTargetCount++] = RHIFormat::R16G16B16A16Float;
	}

	RasterizerPreset GetRasterizerPreset(uint64_t variantBits) noexcept
	{
		return RenderQueueBuilder::DecodeVariantDoubleSided(variantBits) ?
			RasterizerPreset::TwoSided :
			RasterizerPreset::Default;
	}

	bool AreSceneAttachmentExtentsCompatible(const DepthCoverageRasterDomain& domain,
		const RHITextureDesc& color, const RHITextureDesc& depth,
		std::span<const RHITextureDesc> diagnostics) noexcept
	{
		if (!AreDepthCoverageTargetExtentsCompatible(domain, color, depth))
		{
			return false;
		}
		for (const RHITextureDesc& diagnostic : diagnostics)
		{
			if (!AreDepthCoverageTargetExtentsCompatible(domain, diagnostic, depth))
			{
				return false;
			}
		}
		return true;
	}

	void DeclareSceneInputs(RenderGraph::RGBuilder& builder, const RenderFrameContext& context,
		const RenderServices& services, RenderViewID viewId, CompositionDomain domain,
		SceneInputs& inputs) noexcept
	{
		const bool postTemporal = domain == CompositionDomain::PostTemporal;
		auto& blackboard = builder.GetBlackboard();
		auto& targetsTable = blackboard.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
		auto& displayTargets = targetsTable.GetViewTargets(viewId);
		auto& iblRes = blackboard.Get<RGIBLResources>(IBLResourcesName);
		auto& shadowRes = blackboard.Get<RGShadowResources>(ShadowResourcesName);
		const auto& sceneDepth = blackboard.Get<RGSceneDepthResources>(SceneDepthResourcesName);
		const auto& displayDepth =
			blackboard.Get<RGDisplayDepthResources>(DisplayDepthResourcesName);
		const auto& framePlan = blackboard.Get<DepthCoverageFramePlan>(DepthCoverageFramePlanName);
		const auto& renderQueue = context.GetRenderQueue(viewId);
		// The in-place write versions the blackboard entry itself.
		RGTextureId& color =
			postTemporal ? displayTargets.m_DisplayColor : displayTargets.m_SceneColor;
		const RGTextureId depth = postTemporal ? displayDepth.m_Texture : sceneDepth.m_Texture;
		const DepthConvention depthConvention =
			postTemporal ? displayDepth.m_Convention : sceneDepth.m_Convention;

		builder.ReadWriteInPlace(color, RGTextureAccess::RenderTarget);
		inputs.m_SceneColor = color;
		std::array<RHITextureDesc, MaterialDiagnosticTargetCount> diagnosticDescs{};
		inputs.m_MaterialDiagnostics = displayTargets.m_MaterialDiagnosticColor.IsValid();
		if (inputs.m_MaterialDiagnostics)
		{
			const std::array diagnostics{ &displayTargets.m_MaterialDiagnosticColor,
				&displayTargets.m_MaterialDiagnosticCoverage, &displayTargets.m_MaterialDiagnosticLighting };
			static_assert(diagnostics.size() == MaterialDiagnosticTargetCount);
			for (size_t index = 0; index < diagnostics.size(); ++index)
			{
				diagnosticDescs[index] = builder.GetTextureDesc(*diagnostics[index]);
				builder.ReadWriteInPlace(*diagnostics[index], RGTextureAccess::RenderTarget);
				inputs.m_MaterialDiagnosticRtvs[index] =
					builder.CreateView<RHITextureViewType::RenderTarget>(*diagnostics[index]);
			}
		}
		inputs.m_IrradianceCubemap = builder.Read(iblRes.m_IrradianceCubemap, RGTextureAccess::Sample);
		inputs.m_PrefilteredSpecularCubemap =
			builder.Read(iblRes.m_PrefilteredSpecularCubemap, RGTextureAccess::Sample);
		inputs.m_BrdfLut = builder.Read(iblRes.m_BrdfLut, RGTextureAccess::Sample);
		const auto* atmosphere = blackboard.TryGet<RGAtmosphereResources>(AtmosphereResourcesName);
		if (atmosphere && context.m_RenderScene.m_WorldSunLightIndex != std::numeric_limits<uint32_t>::max())
		{
			inputs.m_AtmosphereTransmittance = builder.Read(atmosphere->m_Luts[0],
				RGTextureAccess::Sample, RHIStage::PixelShader);
			inputs.m_AtmosphereTransmittanceSrv =
				builder.CreateView<RHITextureViewType::ShaderResource>(inputs.m_AtmosphereTransmittance);
		}
		const auto& shadowPlan = context.GetDirectionalShadowFramePlan();
		if (shadowPlan.m_ShadingEnabled)
		{
			inputs.m_ShadowMap = builder.Read(shadowRes.m_DirectionalShadowMap, RGTextureAccess::Sample);
			const auto shadowSrvDesc = MakeRHITexture2DArrayViewDesc(RHIFormat::R32Float, 0, 0,
				shadowRes.m_CascadeCount, RHITextureAspect::Depth);
			inputs.m_ShadowSrv = builder.CreateView<RHITextureViewType::ShaderResource>(
				inputs.m_ShadowMap, shadowSrvDesc);
		}
		inputs.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(inputs.m_SceneColor);

		inputs.m_RasterDomain = postTemporal ?
			std::addressof(renderQueue.m_PostTemporalRasterDomain) :
			std::addressof(renderQueue.m_CoverageRasterDomain);
		inputs.m_ExpectedRenderQueue = framePlan.m_SourceRenderQueue;
		inputs.m_ShadowMapSize = shadowRes.m_ShadowMapSize;
		if (!renderQueue.m_DrawItems.empty())
		{
			GGLAB_ASSERT_MSG(inputs.m_RasterDomain->IsValid(),
				"Forward rendering requires a valid raster domain.");
			GGLAB_ASSERT_MSG(inputs.m_RasterDomain->m_DepthConvention == depthConvention,
				"Forward raster and resource depth conventions must match.");
			GGLAB_ASSERT_MSG(AreSceneAttachmentExtentsCompatible(*inputs.m_RasterDomain,
				builder.GetTextureDesc(color), builder.GetTextureDesc(depth),
				inputs.m_MaterialDiagnostics ?
					std::span<const RHITextureDesc>(diagnosticDescs) :
					std::span<const RHITextureDesc>{}),
				"Every Forward color, diagnostic and depth attachment must match the raster domain.");
			GGLAB_ASSERT_MSG(postTemporal || framePlan.m_RasterDomain == inputs.m_RasterDomain,
				"Pre-temporal Forward must consume the frame-plan raster domain.");
			GGLAB_ASSERT_MSG(inputs.m_ExpectedRenderQueue == std::addressof(renderQueue),
				"Forward must consume the frame-plan RenderQueue and its shared draw packets.");
		}

		// The depth prepass owns depth writes; Forward shading only reads them.
		inputs.m_Depth = builder.Read(depth, RGTextureAccess::DepthStencilRead);
		RHITextureViewDesc readOnlyDsvDesc =
			postTemporal ? displayDepth.m_DsvDesc : sceneDepth.m_DsvDesc;
		readOnlyDsvDesc.m_ReadOnlyDepth = true;
		inputs.m_Dsv = builder.CreateView<RHITextureViewType::DepthStencil>(inputs.m_Depth, readOnlyDsvDesc);

		inputs.m_ShadowSamplerIndex =
			services.m_Samplers->GetSamplerIndex(SamplerPreset::ShadowCmpLinearClamp);
		inputs.m_ShadowFlags = (shadowPlan.m_ShadingEnabled ? 1u : 0u) |
			(shadowPlan.m_Settings.m_EnablePCF ? 2u : 0u) |
			(context.GetShadowVisualizationSettings().m_ShowCascadeOverlay ? 4u : 0u) |
			(context.GetShadowVisualizationSettings().m_ShowTransitionOverlay ? 8u : 0u);
	}

	RHIRenderingAttachment GetSceneColorAttachment(
		RGExecuteContext& executeContext, const SceneInputs& inputs) noexcept
	{
		return { .m_View = executeContext.GetViewHandle(inputs.m_Rtv) };
	}

	RHIRenderingAttachment GetDepthAttachment(
		RGExecuteContext& executeContext, const SceneInputs& inputs) noexcept
	{
		return { .m_View = executeContext.GetViewHandle(inputs.m_Dsv) };
	}

	void AppendMaterialDiagnosticAttachments(RGExecuteContext& executeContext,
		const SceneInputs& inputs, std::span<RHIRenderingAttachment> attachments,
		uint32_t& attachmentCount) noexcept
	{
		if (!inputs.m_MaterialDiagnostics)
		{
			return;
		}
		GGLAB_ASSERT(attachmentCount + MaterialDiagnosticTargetCount <= attachments.size());
		for (const auto view : inputs.m_MaterialDiagnosticRtvs)
		{
			attachments[attachmentCount++] = { .m_View = executeContext.GetViewHandle(view) };
		}
	}

	PassParameters ResolveScenePassParameters(RGExecuteContext& executeContext,
		const SceneInputs& inputs, const RenderServices& services, RenderViewID viewId) noexcept
	{
		const auto shadowSrv = inputs.m_ShadowSrv.IsValid() ?
			executeContext.GetViewDescriptor(inputs.m_ShadowSrv) :
			RHIDescriptorHandle{};
		GGLAB_ASSERT_MSG((inputs.m_ShadowFlags & 1u) == 0 || shadowSrv.IsValid(),
			"Enabled shadow sampling requires a valid shadow descriptor.");

		uint32_t atmosphereTransmittanceIndex = std::numeric_limits<uint32_t>::max();
		if (inputs.m_AtmosphereTransmittanceSrv.IsValid())
		{
			const auto transmittanceSrv =
				executeContext.GetViewDescriptor(inputs.m_AtmosphereTransmittanceSrv);
			GGLAB_ASSERT_MSG(transmittanceSrv.IsValid(),
				"Physical sun attenuation requires a transmittance descriptor.");
			atmosphereTransmittanceIndex = transmittanceSrv.m_Index;
		}
		// The raster domain selects the view data: a post-temporal domain may bind its own
		// unjittered display view.
		const bool domainBound = inputs.m_RasterDomain && inputs.m_RasterDomain->IsValid();
		return {
			.m_ViewIndex = domainBound ?
				inputs.m_RasterDomain->m_ViewBindingId :
				static_cast<uint32_t>(utils::ToIndex(viewId)),
			.m_ShadowMapTextureIndex = shadowSrv.IsValid() ? shadowSrv.m_Index : 0u,
			.m_ShadowMapSamplerIndex = inputs.m_ShadowSamplerIndex,
			.m_ShadowMapSize = inputs.m_ShadowMapSize,
			.m_ShadowFlags = inputs.m_ShadowFlags,
			.m_AtmosphereTransmittanceIndex = atmosphereTransmittanceIndex,
			.m_AtmosphereSamplerIndex = services.m_Samplers->GetSamplerIndex(SamplerPreset::LinearClamp),
		};
	}

	const DrawItemsRange* FindFirstDrawRange(
		const RenderQueue& renderQueue, std::span<const RenderBucket> buckets) noexcept
	{
		for (const RenderBucket bucket : buckets)
		{
			const auto& range = renderQueue.m_BucketDrawRanges[utils::ToIndex(bucket)];
			if (range.m_Count > 0)
			{
				GGLAB_ASSERT_MSG(range.m_Start < renderQueue.m_DrawItems.size(),
					"Forward first draw must be inside the frame-plan RenderQueue.");
				return range.m_Start < renderQueue.m_DrawItems.size() ? std::addressof(range) : nullptr;
			}
		}
		return nullptr;
	}

	void BindSceneResources(RHIGraphicsCommandContext& graphicsContext,
		const RenderFrameContext& context, const RenderServices& services,
		const SceneInputs& inputs, const RenderQueue& renderQueue,
		uint32_t shadowParameterIndex) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(inputs.m_RasterDomain);
		GGLAB_ASSERT_MSG(inputs.m_RasterDomain == std::addressof(renderQueue.m_CoverageRasterDomain) ||
			inputs.m_RasterDomain == std::addressof(renderQueue.m_PostTemporalRasterDomain),
			"Forward must consume a RenderQueue raster domain directly.");
		graphicsContext.SetViewport(inputs.m_RasterDomain->m_Viewport);
		graphicsContext.SetScissorRect(inputs.m_RasterDomain->m_Scissor);
		graphicsContext.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

		graphicsContext.SetConstantBuffer(
			static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB),
			context.m_FrameConstants.m_Scene);
		graphicsContext.SetConstantBuffer(
			shadowParameterIndex,
			context.m_FrameConstants.m_DirectionalShadow);

		const auto& objectSB = services.m_FrameBuffers->GetObjectStructuredBuffer();
		graphicsContext.SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::ObjectSB),
			objectSB->GetBufferHandle(context.m_FrameSlotIndex));
		const auto& materialSB = services.m_FrameBuffers->GetMaterialStructuredBuffer();
		graphicsContext.SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::MaterialSB),
			materialSB->GetBufferHandle(context.m_FrameSlotIndex));
		if (!graphicsContext.SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB),
			context.m_FrameConstants.m_Views))
		{
			return;
		}
		const auto& lightSB = services.m_FrameBuffers->GetLightStructuredBuffer();
		graphicsContext.SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::LightSB),
			lightSB->GetBufferHandle(context.m_FrameSlotIndex));
	}
}
