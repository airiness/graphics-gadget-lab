#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/RenderPass/RenderPassRecipeRegistry.h"

namespace gglab
{
	class DX12PipelineState;
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

		DX12PipelineState* GetOrCreatePSO(const Renderer& renderer) noexcept;

	private:
		GraphicsPipelineRecipe m_BaseRecipe{};
		bool m_IsInitialized = false;
	};
}
