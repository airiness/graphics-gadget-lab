#pragma once

#include <filesystem>

namespace gglab
{
	// Resolves an absolute path unchanged, or a logical path under an injected
	// asset root. The legacy "Assets/..." prefix remains a stable logical spelling.
	[[nodiscard]] std::filesystem::path ResolveAssetPath(
		const std::filesystem::path& assetRoot, const std::filesystem::path& path) noexcept;
}
