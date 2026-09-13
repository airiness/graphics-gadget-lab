#pragma once

namespace gglab
{
	class Renderer;
	class RenderGraph;
	struct RenderView;
	struct ResolvedTemporalFramePlan;
	struct TemporalAASettings;
	struct TemporalAADiagnosticsSnapshot;

	[[nodiscard]] TemporalAADiagnosticsSnapshot BuildTemporalAADiagnosticsSnapshot(
		const Renderer& renderer, const RenderGraph& renderGraph,
		const ResolvedTemporalFramePlan* framePlan, const RenderView* displayView,
		const TemporalAASettings* authoringSettings,
		const TemporalAASettings* requestedSettings) noexcept;
}
