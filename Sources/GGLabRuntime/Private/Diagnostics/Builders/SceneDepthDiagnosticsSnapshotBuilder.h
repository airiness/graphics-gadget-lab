#pragma once

namespace gglab
{
	class RenderGraph;
	struct SceneDepthDiagnosticsSnapshot;

	[[nodiscard]] SceneDepthDiagnosticsSnapshot BuildSceneDepthDiagnosticsSnapshot(
		const RenderGraph& renderGraph) noexcept;
}
