#pragma once

#include "GGLabRuntime/Diagnostics/Snapshots/RenderingSettingsDiagnosticsSnapshot.h"

namespace gglab
{
	struct DiagnosticsFrameContext;

	[[nodiscard]] RenderingSettingsDiagnosticsSnapshot BuildRenderingSettingsDiagnosticsSnapshot(
		const DiagnosticsFrameContext& context,
		const TemporalHistorySummary* history = nullptr) noexcept;
}
