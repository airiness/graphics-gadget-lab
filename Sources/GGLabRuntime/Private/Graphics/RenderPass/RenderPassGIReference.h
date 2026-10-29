#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIReference.h"

#include <memory>

namespace gglab
{
	class RenderGraph;
	struct RaySceneGraphResources;

	// Stateless, per-frame one-bounce estimator. No submitted-sample advancement,
	// persistent accumulation, field lookup, denoiser or temporal history.
	class RenderPassGIReference final
	{
	public:
		RenderPassGIReference() noexcept;
		~RenderPassGIReference();
		[[nodiscard]] RenderFrameValidationResult Prepare(const RenderFrameContext& frame,
			const RenderServices& services, const GIReferenceCreateInfo& info) noexcept;
		void AddPass(RenderGraph& graph, const RenderFrameContext& frame,
			const RenderServices& services, const RaySceneGraphResources& scene,
			const GIReferenceCreateInfo& info) noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
