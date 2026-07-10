#pragma once

namespace gglab
{
	class Renderer;
	struct IBLDiagnosticsSnapshot;

	[[nodiscard]] IBLDiagnosticsSnapshot BuildIBLDiagnosticsSnapshot(const Renderer& renderer) noexcept;
}
