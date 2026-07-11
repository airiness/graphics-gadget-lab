#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"

namespace gglab
{
	class RenderPassIBLClear : public RenderPassBase
	{
	public:
		RenderPassIBLClear() noexcept : RenderPassBase({
			.m_TypeName = "IBL.ClearFallback",
			.m_DisplayName = "IBL Clear Fallback",
			.m_CategoryName = "IBL",
			.m_Description = "Initializes the active IBL set to deterministic black resources before the first bake is published.",
			.m_Category = RenderPassCategory::IBL,
			.m_Type = RenderPassType::Graphics,
		}) {}

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;
		void AddBakePass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept;
	};
}
