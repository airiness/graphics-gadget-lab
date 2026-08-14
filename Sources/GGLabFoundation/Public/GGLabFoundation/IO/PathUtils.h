#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace gglab::utils
{
	[[nodiscard]] bool ExtensionEqualsAsciiIgnoreCase(
		const std::filesystem::path& path, std::string_view extension) noexcept;
	[[nodiscard]] std::filesystem::path Canonical(const std::filesystem::path& path) noexcept;
	[[nodiscard]] bool CreateDirectoryIfNotExist(
		const std::filesystem::path& directory) noexcept;
	[[nodiscard]] bool CreateParentDirectoryIfNotExist(
		const std::filesystem::path& file) noexcept;
	[[nodiscard]] std::int64_t LastWriteTimeTicks(
		const std::filesystem::path& path) noexcept;
	[[nodiscard]] bool WriteFileBinary(
		const std::filesystem::path& file, std::span<const std::byte> bytes) noexcept;
}
