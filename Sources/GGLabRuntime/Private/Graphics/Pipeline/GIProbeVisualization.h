#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIFieldObservation.h"

#include <array>
#include <cstdint>
#include <memory>

namespace gglab
{
	class RenderGraph;
	struct RenderFrameContext;
	struct RenderServices;

	class GIProbeVisualization final
	{
	public:
		GIProbeVisualization() noexcept;
		~GIProbeVisualization();
		void Prepare(const RenderFrameContext& frame, const RenderServices& services,
			GIProbeVisualizationSettings settings, uint64_t generation,
			const std::array<uint32_t, 3>& counts) noexcept;
		void AddPasses(RenderGraph& graph, const RenderFrameContext& frame,
			const GIObservationStamp& observation) noexcept;
		[[nodiscard]] GIProbeVisualizationSnapshot GetSnapshot() const noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
