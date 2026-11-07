#pragma once
#include <memory>

namespace gglab
{
	class RenderGraph;
	struct RenderFrameContext;
	struct RenderServices;

	// Controlled post-interpolation D32 values, independent of scene projection.
	class GIProbeDepthConformance final
	{
	public:
		GIProbeDepthConformance() noexcept;
		~GIProbeDepthConformance();
		void AddPasses(RenderGraph& graph, const RenderFrameContext& frame,
			const RenderServices& services) noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
