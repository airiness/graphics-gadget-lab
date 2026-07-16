#pragma once

namespace gglab
{
	class Renderer;
	class RenderGraph;
	struct PostProcessDiagnosticsSnapshot;

	[[nodiscard]] PostProcessDiagnosticsSnapshot BuildPostProcessDiagnosticsSnapshot(
		const Renderer& renderer,
		const RenderGraph& renderGraph) noexcept;
}
