#pragma once
#include "RenderPassBase.h"
#include "RenderPassRecipeRegistry.h"

namespace gglab
{
	class RenderPassForwardPBR : public RenderPassBase
	{
	public:
		RenderPassForwardPBR() noexcept;
		~RenderPassForwardPBR() = default;

		void AddPass(RenderGraph& rg) noexcept override;

	private:
		GraphicsKeyInputs m_KeyInputs{};
	};
}