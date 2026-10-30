#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIReference.h"

#include <memory>

namespace gglab
{
	class RenderGraph;
	struct RaySceneGraphResources;

	// Fixed-depth diffuse estimator backed by the Renderer-owned bounded observation.
	// No field lookup, denoiser or cross-observation temporal history.
	class RenderPassGIReference final
	{
	public:
		RenderPassGIReference() noexcept;
		~RenderPassGIReference();
		[[nodiscard]] RenderFrameValidationResult Prepare(const RenderFrameContext& frame,
			const RenderServices& services, const GIReferenceCreateInfo& info,
			ShaderID coverageVertex) noexcept;
		void AddPass(RenderGraph& graph, const RenderFrameContext& frame,
			const RenderServices& services, const RaySceneGraphResources& scene,
			const GIReferenceCreateInfo& info) noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
