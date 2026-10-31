#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"

#include <memory>

namespace gglab
{
	class RenderGraph;
	struct RenderFrameContext;
	struct RenderServices;
	struct RaySceneGraphResources;

	// Compares the immutable ray snapshot's reduced GI material with raster receivers.
	// Compose after Forward+'s optional GI receiver pass and before RayScene table exports.
	class RaySceneHitMaterial final
	{
	public:
		RaySceneHitMaterial() noexcept;
		~RaySceneHitMaterial();
		GGLAB_DELETE_COPYABLE_MOVABLE(RaySceneHitMaterial);
		[[nodiscard]] bool Prepare(const RenderServices& services) noexcept;
		void AddPass(RenderGraph& graph, const RenderFrameContext& frame,
			const RenderServices& services, const RaySceneGraphResources& scene) noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
