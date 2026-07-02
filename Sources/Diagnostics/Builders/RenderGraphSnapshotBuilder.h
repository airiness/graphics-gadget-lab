#pragma once

namespace gglab
{
	class RenderGraph;
	struct RGSnapshot;

	void BuildRenderGraphSnapshot(const RenderGraph& renderGraph, RGSnapshot& outSnapshot) noexcept;
}
