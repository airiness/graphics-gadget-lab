#pragma once

namespace gglab
{
	class Renderer;
	class RenderGraph;
	struct AtmosphereDiagnosticsSnapshot;

	[[nodiscard]] AtmosphereDiagnosticsSnapshot BuildAtmosphereDiagnosticsSnapshot(
		const Renderer& renderer, const RenderGraph& renderGraph) noexcept;
}
