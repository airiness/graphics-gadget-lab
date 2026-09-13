#pragma once

namespace gglab
{
	class DiagnosticsRuntime;
	class DX12Context;
	class PipelineCache;

	void RegisterDX12SnapshotProviders(DiagnosticsRuntime& runtime, DX12Context& context,
		PipelineCache* pipelineCache) noexcept;
}
