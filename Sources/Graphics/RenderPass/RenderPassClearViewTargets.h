#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"

namespace gglab
{
	class RenderPassClearViewTargets : public RenderPassBase
	{
	public:
		RenderPassClearViewTargets() noexcept : RenderPassBase({
			.m_TypeName = "View.ClearTargets",
			.m_DisplayName = "Clear View Targets",
			.m_CategoryName = "Geometry",
			.m_Description = "Clears the display view HDR color and depth targets before background and geometry rendering.",
			.m_Category = RenderPassCategory::Geometry,
			.m_Type = RenderPassType::Graphics,
		}) {}

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;
	};
}
