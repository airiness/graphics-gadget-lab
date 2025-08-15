#pragma once

namespace graphicsGadgetLab
{
	class RenderGraph;
	class RenderPassBase
	{
	public:
		virtual ~RenderPassBase() = default;
		virtual void AddPass(RenderGraph& rg) = 0;
	};
}