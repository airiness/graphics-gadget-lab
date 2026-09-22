#pragma once

namespace gglab
{
	class RenderGraph;
	struct DirectionalShadowCascadeSet;
	struct ShadowDiagnosticsSnapshot;

	[[nodiscard]] ShadowDiagnosticsSnapshot BuildShadowDiagnosticsSnapshot(
		const RenderGraph& renderGraph, const DirectionalShadowCascadeSet* cascades = nullptr) noexcept;
}
