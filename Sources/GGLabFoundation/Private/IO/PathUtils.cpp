#include "GGLabFoundation/IO/PathUtils.h"
#include "GGLabFoundation/String/StringUtils.h"

#include <chrono>
#include <fstream>

namespace gglab::utils
{
	bool ExtensionEqualsAsciiIgnoreCase(
		const std::filesystem::path& path, std::string_view extension) noexcept
	{
		return EqualsAsciiIgnoreCase(path.extension().string(), extension);
	}

	std::filesystem::path Canonical(const std::filesystem::path& path) noexcept
	{
		std::error_code errorCode;
		auto weak = std::filesystem::weakly_canonical(path, errorCode);
		if (!errorCode)
		{
			return weak.make_preferred();
		}

		auto absolute = std::filesystem::absolute(path, errorCode);
		if (!errorCode)
		{
			return absolute.lexically_normal().make_preferred();
		}

		return path.lexically_normal().make_preferred();
	}

	bool CreateDirectoryIfNotExist(const std::filesystem::path& directory) noexcept
	{
		std::error_code errorCode;
		if (std::filesystem::exists(directory, errorCode))
		{
			return true;
		}
		return std::filesystem::create_directories(directory, errorCode);
	}

	bool CreateParentDirectoryIfNotExist(const std::filesystem::path& file) noexcept
	{
		return CreateDirectoryIfNotExist(file.parent_path());
	}

	std::int64_t LastWriteTimeTicks(const std::filesystem::path& path) noexcept
	{
		std::error_code errorCode;
		if (!std::filesystem::exists(path, errorCode))
		{
			return 0;
		}

		const auto time = std::filesystem::last_write_time(path, errorCode);
		return errorCode
			? 0
			: std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch())
			.count();
	}

	bool WriteFileBinary(
		const std::filesystem::path& file, std::span<const std::byte> bytes) noexcept
	{
		if (!CreateParentDirectoryIfNotExist(file))
		{
			return false;
		}

		std::ofstream output(file, std::ios::binary);
		if (!output)
		{
			return false;
		}
		if (!bytes.empty())
		{
			output.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		}
		return static_cast<bool>(output);
	}
}
