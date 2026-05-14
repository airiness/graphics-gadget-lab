#pragma once
#include "Graphics/RenderContexts.h"

#include <string_view>

namespace gglab
{
	struct RenderView;
	struct RenderScene;
	class Renderer;
	class RenderGraph;

	class RenderPipelineBase
	{
	public:
		RenderPipelineBase() noexcept = default;
		virtual ~RenderPipelineBase() = default;

		virtual std::string_view GetName() const noexcept = 0;

		virtual void BuildRenderGraph(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept = 0;
	};
}