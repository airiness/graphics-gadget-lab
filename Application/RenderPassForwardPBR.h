#pragma once
#include "RenderPassBase.h"
#include "RenderPassRecipeRegistry.h"

namespace gglab
{
	class DX12CommandList;
	class RenderPassForwardPBR : public RenderPassBase
	{
	public:
		RenderPassForwardPBR() noexcept = default;
		~RenderPassForwardPBR() override = default;

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		void EnsureInitialized(const RenderServices& services) noexcept;

		void DrawModels(DX12CommandList* commandList,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept;

	private:
		GraphicsKeyInputs m_KeyInputs{};

		bool m_IsInitialized = false;
	};
}