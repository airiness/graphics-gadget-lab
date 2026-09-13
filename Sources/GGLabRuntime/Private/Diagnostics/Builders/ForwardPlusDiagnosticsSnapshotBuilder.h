#pragma once

namespace gglab
{
	class Renderer;
	class RenderGraph;
	struct ForwardPlusDiagnosticsSnapshot;

	[[nodiscard]] ForwardPlusDiagnosticsSnapshot BuildForwardPlusDiagnosticsSnapshot(
		const Renderer& renderer, const RenderGraph& renderGraph) noexcept;
}
