#pragma once

namespace gglab
{
	class AssetManager;
	struct AssetSnapshot;

	// Synchronous owner-thread read of the current asset subsystem state. The
	// returned snapshot owns copied values and retains no asset lifetime. This is
	// the narrow read entry for hosts that inspect live state outside the
	// diagnostics capture window; ordinary tooling reads published snapshots.
	[[nodiscard]] AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept;
}
