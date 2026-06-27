#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/PipelineCache.h"

namespace gglab
{
	class Renderer;

	class RenderPassTonemap final : public RenderPassBase
	{
	public:
		RenderPassTonemap() noexcept = default;
		~RenderPassTonemap() override = default;

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		RHIPipelineHandle GetOrCreatePSO(const Renderer& renderer) noexcept;

	private:
		GraphicsPipelineRecipe m_BaseRecipe{};
		GraphicsPipelineSlot m_PipelineSlot{};
		bool m_IsInitialized = false;
	};
}
