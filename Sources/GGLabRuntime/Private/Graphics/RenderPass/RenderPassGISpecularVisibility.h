#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GISpecularVisibility.h"
#include "GGLabRuntime/Graphics/GraphicsHandles.h"

#include <memory>

namespace gglab
{
	class RenderGraph;
	struct RenderFrameContext;
	struct RenderServices;
	struct RaySceneGraphResources;

	class RenderPassGISpecularVisibility final
	{
	public:
		RenderPassGISpecularVisibility() noexcept;
		~RenderPassGISpecularVisibility();
		[[nodiscard]] RenderFrameValidationResult Prepare(const RenderFrameContext& frame,
			const RenderServices& services, const GISpecularVisibilityCreateInfo& info,
			ShaderID coverageVertex) noexcept;
		void AddPass(RenderGraph& graph, const RenderFrameContext& frame,
			const RenderServices& services, const RaySceneGraphResources& scene,
			const GISpecularVisibilityCreateInfo& info) noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
