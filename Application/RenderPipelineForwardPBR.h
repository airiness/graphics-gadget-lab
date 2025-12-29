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
			bool m_EnableDevelopGuiPass = true;
		};

	public:
		RenderPipelineForwardPBR() noexcept = default;
		~RenderPipelineForwardPBR() override = default;

		std::string_view GetName() const noexcept override { return "ForwardPBR"; }

		void BuildRenderGraph(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

		bool IsRenderPassForwardPBREnabled() const noexcept { return m_Settings.m_EnableForwardPBRPass; }
		bool IsRenderPassDevelopGuiEnabled() const noexcept { return m_Settings.m_EnableDevelopGuiPass; }

	private:
		Settings m_Settings{};
		RenderPassForwardPBR m_ForwardPBRPass;
	};
}