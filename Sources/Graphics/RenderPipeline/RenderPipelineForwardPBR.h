#pragma once
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Graphics/RenderPass/RenderPassForwardPBR.h"
#include "Graphics/RenderPass/RenderPassIBLBrdfLUT.h"

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
		RenderPassForwardPBR m_ForwardPBRPass;
		RenderPassIBLBrdfLUT m_IBLBrdfLUTPass;
	};
}