#pragma once

namespace gglab::utils
{
	std::filesystem::path Canonical(const std::filesystem::path& path) noexcept;
	bool CreateDirectoryIfNotExist(const std::filesystem::path& dir) noexcept;
	bool CreateParentDirectoryIfNotExist(const std::filesystem::path& file) noexcept;
	int64_t LastWriteTimeTicks(const std::filesystem::path& path) noexcept;
	bool WriteFileBinary(const std::filesystem::path& file, const void* data, size_t size) noexcept;
	bool LinkDirectory(const std::filesystem::path& srcDir, const std::filesystem::path& dstDir) noexcept;
	std::filesystem::path GetModuleDirectory(HMODULE hModule) noexcept;
	std::filesystem::path GetExeOutDir() noexcept;
}
