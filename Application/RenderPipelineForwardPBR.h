#pragma once
#include "RenderPipelineBase.h"
#include "RenderPassForwardPBR.h"

namespace gglab
{
	class RenderPipelineForwardPBR : public RenderPipelineBase
	{
	public:
		struct Settings
		{
			bool m_EnableForwardPBRPass = true;
		};

	public:
		RenderPipelineForwardPBR() noexcept = default;
		~RenderPipelineForwardPBR() override = default;

		std::string_view GetName() const noexcept override { return "ForwardPBR"; }

		void BuildRenderGraph(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		Settings m_Settings{};
		RenderPassForwardPBR m_ForwardPBRPass;
	};
}