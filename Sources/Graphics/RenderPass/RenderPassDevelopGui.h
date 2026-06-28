#pragma once
#include "Graphics/RenderPass/RenderPassBase.h"

namespace gglab
{
	class RenderPassDevelopGui : public RenderPassBase
	{
	public:
		RenderPassDevelopGui() noexcept : RenderPassBase({
			.m_TypeName = "UI.DevelopGui",
			.m_DisplayName = "Develop GUI",
			.m_CategoryName = "UI",
			.m_Description = "Composites the engine development UI onto the back buffer.",
			.m_Category = RenderPassCategory::UI,
			.m_Type = RenderPassType::Graphics,
			.m_EnableProfiling = false,
		}) {}
		~RenderPassDevelopGui() override = default;

		void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override;
	};
}
