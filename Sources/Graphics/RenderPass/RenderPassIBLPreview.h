#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/RenderPass/RenderPassRecipeRegistry.h"

namespace gglab
{
	class DX12PipelineState;
	class Renderer;

	class RenderPassIBLPreview : public RenderPassBase
	{
	public:
		RenderPassIBLPreview() noexcept = default;
		~RenderPassIBLPreview() override = default;

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		DX12PipelineState* GetOrCreateCubemapPreviewPSO(const Renderer& renderer) noexcept;

	private:
		GraphicsPipelineRecipe m_CubemapPreviewRecipe{};
		bool m_IsInitialized = false;
	};
}
