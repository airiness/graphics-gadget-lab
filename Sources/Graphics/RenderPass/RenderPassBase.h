#pragma once
#include "Graphics/RenderContexts.h"

namespace gglab
{
	class RenderGraph;
	class RenderPassBase
	{
	public:
		virtual ~RenderPassBase() = default;
		virtual void AddPass(RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) = 0;
	};
}