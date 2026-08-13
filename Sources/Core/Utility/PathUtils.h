#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace gglab::utils
{
	[[nodiscard]] bool ExtensionEqualsIgnoreCase(
		const std::filesystem::path& path, std::string_view extension) noexcept;
	std::filesystem::path Canonical(const std::filesystem::path& path) noexcept;
	bool CreateDirectoryIfNotExist(const std::filesystem::path& dir) noexcept;
	bool CreateParentDirectoryIfNotExist(const std::filesystem::path& file) noexcept;
	int64_t LastWriteTimeTicks(const std::filesystem::path& path) noexcept;
	bool WriteFileBinary(const std::filesystem::path& file, const void* data, size_t size) noexcept;
}
