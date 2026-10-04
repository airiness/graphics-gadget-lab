#include "Graphics/RenderPass/RenderPassForwardTransparent.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RenderPass/ForwardShadingCommon.h"

#include <array>
#include <memory>
#include <span>

namespace gglab
{
	namespace
	{
		constexpr std::array TransparentBuckets{ RenderBucket::Transparent };
		constexpr size_t MaxTransparentAttachmentCount = 1 + forward_shading::MaterialDiagnosticTargetCount;

		struct PassData
		{
			forward_shading::SceneInputs m_Scene{};
		};
	}

	void RenderPassForwardTransparent::Prepare(
		const RenderServices& services, const ForwardPBRShaderSet& shaderSet) noexcept
	{
		if (!m_IsInitialized)
		{
			GGLAB_ASSERT_MSG(shaderSet.IsValid(),
				"Forward transparent shading requires the shared Forward shader set.");
			if (!shaderSet.IsValid())
			{
				return;
			}
			m_PhysicalKeys[0] = forward_shading::MakeBasePhysicalKey(
				forward_shading::CreateBindingLayout(services, false),
				shaderSet.m_CoverageVertexShader, shaderSet.m_AllLightsShadingPixelShader);
			m_IsInitialized = true;
		}
		if (!m_MaterialDiagnosticPipelineSlots && shaderSet.AreMaterialDiagnosticsValid())
		{
			m_PhysicalKeys[1] = m_PhysicalKeys[0];
			m_PhysicalKeys[1].m_PSId = shaderSet.m_AllLightsMaterialDiagnosticsPixelShader;
			forward_shading::AppendMaterialDiagnosticTargets(m_PhysicalKeys[1]);
			m_MaterialDiagnosticPipelineSlots = std::make_unique<PipelineSlotTable>();
		}
	}

	void RenderPassForwardTransparent::AddPass(
		RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_IsInitialized, "Forward transparent pass must be prepared before graph construction.");
		const auto* contextPtr = &context;
		const RenderViewID displayViewId = context.GetDisplayViewId();

		rg.AddPass<PassData>(
			GetRenderGraphPassName(),
			[contextPtr, services, displayViewId](RenderGraph::RGBuilder& builder, PassData& data)
			{
				builder.SideEffect();
				forward_shading::DeclareSceneInputs(builder, *contextPtr, services, displayViewId, data.m_Scene);
				GGLAB_ASSERT_MSG(builder.GetBlackboard().Get<DepthCoverageFramePlan>(DepthCoverageFramePlanName)
					.AddsForwardTransparentPass(),
					"Forward transparent shading is added only for validated frame plans with transparent draws.");
			},
			[this, contextPtr, services, displayViewId](RGExecuteContext& executeContext, PassData& data)
			{
				auto* graphicsContext = executeContext.GetGraphicsCommandContext();
				GGLAB_ASSERT_NOT_NULL(graphicsContext);

				std::array<RHIRenderingAttachment, MaxTransparentAttachmentCount> renderTargets{};
				renderTargets[0] = forward_shading::GetSceneColorAttachment(executeContext, data.m_Scene);
				uint32_t renderTargetCount = 1;
				forward_shading::AppendMaterialDiagnosticAttachments(
					executeContext, data.m_Scene, renderTargets, renderTargetCount);
				graphicsContext->BeginRendering({
					.m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(renderTargets.data(), renderTargetCount),
					.m_DepthAttachment = forward_shading::GetDepthAttachment(executeContext, data.m_Scene),
				});

				const forward_shading::PassParameters passParameters =
					forward_shading::ResolveScenePassParameters(
						executeContext, data.m_Scene, services, displayViewId);
				const auto& renderQueue = contextPtr->GetRenderQueue(displayViewId);
				const DrawItemsRange* firstDrawRange =
					forward_shading::FindFirstDrawRange(renderQueue, TransparentBuckets);
				if (!firstDrawRange)
				{
					return;
				}
				const bool materialDiagnostics = data.m_Scene.m_MaterialDiagnostics;
				const auto resolvePipeline = [&](uint64_t variantBits)
					{
						return GetOrCreatePSOForVariant(services, variantBits, materialDiagnostics);
					};
				graphicsContext->SetPipeline(
					resolvePipeline(renderQueue.m_DrawItems[firstDrawRange->m_Start].m_VariantBits));
				forward_shading::BindSceneResources(
					*graphicsContext, *contextPtr, services, data.m_Scene, renderQueue);
				graphicsContext->SetPushConstants(
					static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), passParameters);

				forward_shading::DrawBuckets(*graphicsContext, renderQueue, TransparentBuckets,
					data.m_Scene.m_ExpectedRenderQueue, resolvePipeline);
			});
	}

	bool RenderPassForwardTransparent::PrewarmMaterialDiagnosticVariant(
		const RenderServices& services, uint64_t variantBits) noexcept
	{
		return GetOrCreatePSOForVariant(services, variantBits, true).IsValid();
	}

	RHIPipelineHandle RenderPassForwardTransparent::GetOrCreatePSOForVariant(
		const RenderServices& services, uint64_t variantBits, bool materialDiagnostics) noexcept
	{
		GGLAB_ASSERT((variantBits & ~RenderQueueBuilder::VariantMask) == 0);
		GGLAB_ASSERT_MSG(RenderQueueBuilder::DecodeVariantBucket(variantBits) == RenderBucket::Transparent,
			"Forward transparent shading only draws the transparent bucket.");
		auto* pipelineCache = services.m_PipelineResolver;
		GGLAB_ASSERT_NOT_NULL(pipelineCache);
		GGLAB_ASSERT_MSG(!materialDiagnostics || m_MaterialDiagnosticPipelineSlots,
			"Diagnostic MRT pipelines must be prepared before graph execution.");

		GraphicsPhysicalPipelineKey physicalKey = m_PhysicalKeys[materialDiagnostics ? 1u : 0u];
		physicalKey.m_RasterizerPreset = forward_shading::GetRasterizerPreset(variantBits);
		physicalKey.m_DepthPreset = DepthPreset::ReversedZReadOnly;
		// Material diagnostic coverage blends into every diagnostic target.
		physicalKey.m_BlendPreset =
			materialDiagnostics ? BlendPreset::AlphaBlendAllTargets : BlendPreset::AlphaBlend;

		const size_t slotIndex = static_cast<size_t>(variantBits & RenderQueueBuilder::VariantMask);
		auto& slots = materialDiagnostics ? *m_MaterialDiagnosticPipelineSlots : m_PipelineSlots;
		return pipelineCache->Resolve(slots[slotIndex], physicalKey, GetInfo());
	}
}
