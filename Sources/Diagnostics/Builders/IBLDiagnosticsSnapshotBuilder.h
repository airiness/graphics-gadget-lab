#pragma once

namespace gglab
{
	class Renderer;
	class EnvironmentAssetController;
	struct IBLDiagnosticsSnapshot;

	[[nodiscard]] IBLDiagnosticsSnapshot BuildIBLDiagnosticsSnapshot(
		const Renderer& renderer, const EnvironmentAssetController* environmentAssets) noexcept;
}
