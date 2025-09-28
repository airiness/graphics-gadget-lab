#pragma once
#include "RenderPassBase.h"
#include "RenderPassRecipeRegistry.h"

namespace gglab
{
	class DX12CommandList;
	class RenderGraph;
	class RenderPassTexColor : public RenderPassBase
	{
	public:
		RenderPassTexColor() noexcept;
		~RenderPassTexColor() = default;

		void AddPass(RenderGraph& rg) noexcept override;

	private:
		void DrawModels(DX12CommandList* commandList) noexcept;

	private:
		GraphicsKeyInputs m_KeyInputs{};
	};
}