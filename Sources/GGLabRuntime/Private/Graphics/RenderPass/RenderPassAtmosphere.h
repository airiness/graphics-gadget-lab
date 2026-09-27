#pragma once
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"
namespace gglab
{
	class RenderPassAtmosphere final : public RenderPassBase
	{
	public:
		RenderPassAtmosphere() noexcept : RenderPassBase({
			.m_TypeName = "Atmosphere.Luts", .m_DisplayName = "Atmosphere LUTs",
			.m_CategoryName = "Lighting", .m_Description = "Caches atmosphere transport and sky-view LUTs.",
			.m_Category = RenderPassCategory::Lighting, .m_Type = RenderPassType::Compute }) {}
		void AddPass(RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept override;
		// Call after all LUT consumers, including sky rendering and diagnostic previews.
		void AddFinishPass(RenderGraph& rg) noexcept;
	private:
		ComputePipelineRecipe m_Recipe{};
		ComputePipelineSlot m_Slot{};
	};
}
