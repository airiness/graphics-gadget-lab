#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/RenderPass/RenderPassRecipeRegistry.h"

namespace gglab
{
	class DX12PipelineState;
	class Renderer;

	class RenderPassIBLDebugPreview : public RenderPassBase
	{
	public:
		RenderPassIBLDebugPreview() noexcept = default;
		~RenderPassIBLDebugPreview() override = default;

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		DX12PipelineState* GetOrCreateEnvironmentCubemapPreviewPSO(const Renderer& renderer) noexcept;

	private:
		GraphicsKeyInputs m_EnvironmentCubemapPreviewInputs{};
		bool m_IsInitialized = false;
	};
}
