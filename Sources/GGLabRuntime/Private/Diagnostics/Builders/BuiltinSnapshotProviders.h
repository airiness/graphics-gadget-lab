#pragma once

namespace gglab
{
	class DiagnosticsRuntime;
	class Renderer;

	void RegisterBuiltinSnapshotProviders(
		DiagnosticsRuntime& runtime, Renderer* renderer) noexcept;
}
