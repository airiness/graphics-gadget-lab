#pragma once

namespace gglab
{
	class DiagnosticsRuntime;
	class PipelineCache;
	class RHIContext;

	// Selects the active backend registrar once during diagnostics-session composition.
	// Registered providers borrow backend services; the session must be destroyed first.
	void RegisterBackendSnapshotProviders(DiagnosticsRuntime& runtime, RHIContext& context,
		PipelineCache* pipelineCache) noexcept;
}
