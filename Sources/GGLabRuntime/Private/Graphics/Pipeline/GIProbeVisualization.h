#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIFieldObservation.h"

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
			GIProbeVisualizationSettings settings) noexcept;
		void AddPasses(RenderGraph& graph, const RenderFrameContext& frame,
			const GIObservationStamp& observation) noexcept;
		[[nodiscard]] GIProbeVisualizationSnapshot GetSnapshot() const noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
