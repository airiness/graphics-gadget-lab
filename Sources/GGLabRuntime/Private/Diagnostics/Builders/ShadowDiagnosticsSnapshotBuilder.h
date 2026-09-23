#pragma once

#include <cstdint>

namespace gglab
{
	class RenderGraph;
	struct RenderView;
	struct DirectionalShadowFramePlan;
	struct ShadowDiagnosticsSnapshot;

	[[nodiscard]] ShadowDiagnosticsSnapshot BuildShadowDiagnosticsSnapshot(
		const RenderGraph& renderGraph, const DirectionalShadowFramePlan* cascades = nullptr,
		const RenderView* mainView = nullptr, uint64_t frameSerial = 0) noexcept;
}
