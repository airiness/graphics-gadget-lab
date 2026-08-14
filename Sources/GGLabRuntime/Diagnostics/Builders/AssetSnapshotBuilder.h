#pragma once

namespace gglab
{
	class AssetManager;
	struct AssetSnapshot;

	[[nodiscard]] AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept;
}
