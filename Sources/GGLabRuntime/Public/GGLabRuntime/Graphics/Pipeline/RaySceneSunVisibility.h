#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"

#include <memory>

namespace gglab
{
	class RenderGraph;
	struct RenderFrameContext;
	struct RenderServices;
	struct RaySceneGraphResources;

	// Optional diagnostic composition. Add after Forward+ has published opaque depth
	// and directional shadows, before exporting RayScene imports. Requires a perspective
	// display view and a WorldSun matching the CSM light. It never changes scene radiance.
	class RaySceneSunVisibility final
	{
	public:
		RaySceneSunVisibility() noexcept;
		~RaySceneSunVisibility();
		GGLAB_DELETE_COPYABLE_MOVABLE(RaySceneSunVisibility);
		[[nodiscard]] bool Prepare(const RenderFrameContext& frame, const RenderServices& services) noexcept;
		void AddPass(RenderGraph& graph, const RenderFrameContext& frame,
			const RenderServices& services, const RaySceneGraphResources& scene) noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
