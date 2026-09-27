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
			.m_CategoryName = "Lighting", .m_Description = "Diagnostic atmosphere transport LUTs.",
			.m_Category = RenderPassCategory::Lighting, .m_Type = RenderPassType::Compute }) {}
		void AddPass(RenderGraph& rg, const RenderFrameContext& context, const RenderServices& services) noexcept override;
		// Call after all LUT consumers, including PostProcess preview and overlays.
		void AddFinishPass(RenderGraph& rg) noexcept;
	private:
		ComputePipelineRecipe m_Recipe{};
		ComputePipelineSlot m_Slot{};
	};
}
