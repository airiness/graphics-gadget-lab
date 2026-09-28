#pragma once
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RenderPass/RenderPassBase.h"
#include "Graphics/RenderPass/AerialPerspectiveProbeReadback.h"

namespace gglab
{
	class RenderPassAerialPerspective final : public RenderPassBase
	{
	public:
		RenderPassAerialPerspective() noexcept : RenderPassBase({
			.m_TypeName = "Atmosphere.AerialPerspective", .m_DisplayName = "Aerial Perspective",
			.m_CategoryName = "Lighting", .m_Description = "Integrates atmosphere over opaque view depth.",
			.m_Category = RenderPassCategory::Lighting, .m_Type = RenderPassType::Compute }) {}
		void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept override;

	private:
		ComputePipelineRecipe m_BuildRecipe{};
		ComputePipelineRecipe m_CompositeRecipe{};
		ComputePipelineSlot m_BuildSlot{};
		ComputePipelineSlot m_CompositeSlot{};
		ComputePipelineRecipe m_ProbeRecipe{};
		ComputePipelineSlot m_ProbeSlot{};
		AerialPerspectiveProbeReadback m_ProbeReadback{};
	};
}
