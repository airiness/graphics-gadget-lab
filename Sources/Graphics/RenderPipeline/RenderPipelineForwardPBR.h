#pragma once
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Graphics/RenderPass/RenderPassClearViewTargets.h"
#include "Graphics/RenderPass/RenderPassDevelopGui.h"
#include "Graphics/RenderPass/RenderPassDebugDraw.h"
#include "Graphics/RenderPass/RenderPassDepthPrepass.h"
#include "Graphics/RenderPass/RenderPassDirectionalShadowMap.h"
#include "Graphics/RenderPass/RenderPassForwardOpaque.h"
#include "Graphics/RenderPass/RenderPassForwardPlusCull.h"
#include "Graphics/RenderPass/RenderPassForwardPlusValidation.h"
#include "Graphics/RenderPass/RenderPassForwardTransparent.h"
#include "Graphics/RenderPass/RenderPassIBL.h"
#include "Graphics/RenderPass/RenderPassIBLPreview.h"
#include "Graphics/RenderPass/RenderPassShadowMapPreview.h"
#include "Graphics/RenderPass/RenderPassSkybox.h"
#include "Graphics/RenderPipeline/PostProcessPipeline.h"
#include "Graphics/RenderPipeline/DepthCoverageFramePlan.h"

namespace gglab
{
	class RenderPipelineForwardPBR : public RenderPipelineBase
	{
	public:
		explicit RenderPipelineForwardPBR(
			std::shared_ptr<ForwardPlusDebugReadback> forwardPlusDebugReadback = {}) noexcept :
			m_ForwardPlusCullPass(forwardPlusDebugReadback),
			m_ForwardPlusValidationPass(forwardPlusDebugReadback)
		{
			m_ForwardOpaquePass.SetHdrDiffValidationAvailable(
				forwardPlusDebugReadback != nullptr);
		}
		~RenderPipelineForwardPBR() override = default;

		std::string_view GetName() const noexcept override { return "ForwardPBR"; }

		void BuildRenderGraph(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void PrepareForwardPasses(const RenderServices& services) noexcept;
		[[nodiscard]] DepthCoverageFramePlan BuildDepthCoverageFramePlanForFrame(
			const RenderFrameContext& context, uint32_t targetWidth, uint32_t targetHeight) const;

		RenderPassDirectionalShadowMap m_DirectionalShadowMapPass;
		RenderPassShadowMapPreview m_ShadowMapPreviewPass;
		RenderPassClearViewTargets m_ClearViewTargetsPass;
		RenderPassDepthPrepass m_DepthPrepassPass;
		RenderPassForwardPlusCull m_ForwardPlusCullPass;
		RenderPassForwardPlusValidation m_ForwardPlusValidationPass;
		RenderPassSkybox m_SkyboxPass;
		RenderPassForwardOpaque m_ForwardOpaquePass;
		RenderPassForwardTransparent m_ForwardTransparentPass;
		RenderPassDebugDraw m_DebugDrawScenePass{ DebugDrawPassMode::Scene };
		PostProcessPipeline m_PostProcessPipeline;
		RenderPassIBL m_IBLPass;
		RenderPassIBLPreview m_IBLPreviewPass;
		RenderPassDebugDraw m_DebugDrawOverlayPass{ DebugDrawPassMode::Overlay };
		RenderPassDevelopGui m_DevelopGuiPass;
		ForwardPBRShaderSet m_ForwardPBRShaderSet{};
	};
}
