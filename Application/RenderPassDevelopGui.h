#pragma once
#include "RenderPassBase.h"
#include "RenderPassRecipeRegistry.h"

namespace gglab
{
	class RenderPassDevelopGui : public RenderPassBase
	{
	public:
		RenderPassDevelopGui() noexcept = default;
		~RenderPassDevelopGui() override = default;

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	};
}