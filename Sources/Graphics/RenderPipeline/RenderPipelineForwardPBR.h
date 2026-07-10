#pragma once
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Graphics/RenderPass/RenderPassClearViewTargets.h"
#include "Graphics/RenderPass/RenderPassDevelopGui.h"
#include "Graphics/RenderPass/RenderPassDebugDraw.h"
#include "Graphics/RenderPass/RenderPassDirectionalShadowMap.h"
#include "Graphics/RenderPass/RenderPassForwardPBR.h"
#include "Graphics/RenderPass/RenderPassIBL.h"
#include "Graphics/RenderPass/RenderPassIBLPreview.h"
#include "Graphics/RenderPass/RenderPassShadowMapPreview.h"
#include "Graphics/RenderPass/RenderPassSkybox.h"
#include "Graphics/RenderPass/RenderPassTonemap.h"

namespace gglab
{
	class RenderPipelineForwardPBR : public RenderPipelineBase
	{
	public:
		RenderPipelineForwardPBR() noexcept = default;
		~RenderPipelineForwardPBR() override = default;

		std::string_view GetName() const noexcept override { return "ForwardPBR"; }

		void BuildRenderGraph(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		RenderPassDirectionalShadowMap m_DirectionalShadowMapPass;
		RenderPassShadowMapPreview m_ShadowMapPreviewPass;
		RenderPassClearViewTargets m_ClearViewTargetsPass;
		RenderPassSkybox m_SkyboxPass;
		RenderPassForwardPBR m_ForwardPBRPass;
		RenderPassDebugDraw m_DebugDrawScenePass{ DebugDrawPassMode::Scene };
		RenderPassTonemap m_TonemapPass;
		RenderPassIBL m_IBLPass;
		RenderPassIBLPreview m_IBLPreviewPass;
		RenderPassDebugDraw m_DebugDrawOverlayPass{ DebugDrawPassMode::Overlay };
		RenderPassDevelopGui m_DevelopGuiPass;
	};
}
