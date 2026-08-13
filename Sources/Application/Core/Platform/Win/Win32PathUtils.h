#pragma once

#include <filesystem>

namespace gglab::utils
{
	bool LinkDirectory(
		const std::filesystem::path& srcDir, const std::filesystem::path& dstDir) noexcept;
	[[nodiscard]] std::filesystem::path GetExeOutDir() noexcept;
}
