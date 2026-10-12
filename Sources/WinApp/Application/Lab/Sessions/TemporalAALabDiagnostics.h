#pragma once

#include <string>

namespace gglab
{
	struct LabDiagnosticsFrameContext;
	struct LabDiagnosticsSnapshot;
	struct TemporalAASettings;
	struct TemporalHistoryManagerDiagnostics;

	[[nodiscard]] std::string FormatTemporalAALabSettings(const TemporalAASettings& settings) noexcept;

	void AppendTemporalAALabSettingsDiagnostics(LabDiagnosticsSnapshot& diagnostics,
		const TemporalAASettings& authoring, const LabDiagnosticsFrameContext& context,
		const TemporalHistoryManagerDiagnostics& history) noexcept;
}
