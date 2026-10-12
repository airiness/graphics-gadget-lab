#pragma once

namespace gglab
{
	struct LabDiagnosticsFrameContext;
	struct LabDiagnosticsSnapshot;
	struct TemporalAASettings;
	struct TemporalHistoryManagerDiagnostics;

	void AppendTemporalAALabSettingsDiagnostics(LabDiagnosticsSnapshot& diagnostics,
		const TemporalAASettings& authoring, const LabDiagnosticsFrameContext& context,
		const TemporalHistoryManagerDiagnostics& history) noexcept;
}
