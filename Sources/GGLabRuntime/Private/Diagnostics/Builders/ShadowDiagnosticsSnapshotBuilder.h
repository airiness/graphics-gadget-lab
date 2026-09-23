#pragma once

namespace gglab
{
	class RenderGraph;
	struct DirectionalShadowFramePlan;
	struct ShadowDiagnosticsSnapshot;

	[[nodiscard]] ShadowDiagnosticsSnapshot BuildShadowDiagnosticsSnapshot(
		const RenderGraph& renderGraph, const DirectionalShadowFramePlan* cascades = nullptr) noexcept;
}
