#pragma once
#include "RenderPassBase.h"
#include "RenderPassRecipeRegistry.h"

namespace gglab
{
	class DX12CommandList;
	class RenderPassForwardPBR : public RenderPassBase
	{
	public:
		RenderPassForwardPBR() noexcept;
		~RenderPassForwardPBR() override = default;

		void AddPass(RenderGraph& rg) noexcept override;

	private:
		void DrawModels(DX12CommandList* commandList) noexcept;

	private:
		GraphicsKeyInputs m_KeyInputs{};
	};
}