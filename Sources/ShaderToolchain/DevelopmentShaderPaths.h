#pragma once

#include <filesystem>

namespace gglab
{
	[[nodiscard]] std::filesystem::path ResolveShaderSourceRoot(
		const std::filesystem::path& runtimeRoot) noexcept;
	[[nodiscard]] std::filesystem::path ResolveShaderCacheRoot(
		const std::filesystem::path& runtimeRoot) noexcept;
}
