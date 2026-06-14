#pragma once
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Graphics/RenderPass/RenderPassDevelopGui.h"
#include "Graphics/RenderPass/RenderPassDirectionalShadowMap.h"
#include "Graphics/RenderPass/RenderPassForwardPBR.h"
#include "Graphics/RenderPass/RenderPassIBL.h"
#include "Graphics/RenderPass/RenderPassIBLPreview.h"
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
		RenderPassForwardPBR m_ForwardPBRPass;
		RenderPassTonemap m_TonemapPass;
		RenderPassIBL m_IBLPass;
		RenderPassIBLPreview m_IBLPreviewPass;
		RenderPassDevelopGui m_DevelopGuiPass;
	};
}
