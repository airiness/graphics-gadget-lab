#pragma once

namespace gglab
{
	class RenderGraph;
	struct ShadowDiagnosticsSnapshot;

	[[nodiscard]] ShadowDiagnosticsSnapshot BuildShadowDiagnosticsSnapshot(
		const RenderGraph& renderGraph) noexcept;
}
