#include "Core/Utility/PathUtils.h"
#include "Core/Utility/StringUtils.h"

#include <chrono>
#include <fstream>

namespace gglab::utils
{
	bool ExtensionEqualsIgnoreCase(
		const std::filesystem::path& path, std::string_view extension) noexcept
	{
		return EqualsIgnoreCase(path.extension().string(), extension);
	}

	std::filesystem::path Canonical(const std::filesystem::path& path) noexcept
	{
		std::error_code errorCode;
		auto weak = std::filesystem::weakly_canonical(path, errorCode);
		if (!errorCode)
		{
			return weak.make_preferred();
		}

		auto abs = std::filesystem::absolute(path, errorCode);
		if (!errorCode)
		{
			return abs.lexically_normal().make_preferred();
		}

		return path.lexically_normal().make_preferred();
	}

	bool CreateDirectoryIfNotExist(const std::filesystem::path& dir) noexcept
	{
		std::error_code errorCode;
		if (std::filesystem::exists(dir, errorCode))
		{
			return true;
		}
		return std::filesystem::create_directories(dir, errorCode);
	}

	bool CreateParentDirectoryIfNotExist(const std::filesystem::path& file) noexcept
	{
		return CreateDirectoryIfNotExist(file.parent_path());
	}

	int64_t LastWriteTimeTicks(const std::filesystem::path& path) noexcept
	{
		std::error_code errorCode;

		if (!std::filesystem::exists(path, errorCode))
		{
			return 0;
		}

		auto time = std::filesystem::last_write_time(path, errorCode);
		if (errorCode)
		{
			return 0;
		}

		return std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch())
			.count();
	}

	bool WriteFileBinary(const std::filesystem::path& file, const void* data, size_t size) noexcept
	{
		if (!CreateParentDirectoryIfNotExist(file))
		{
			return false;
		}

		std::ofstream out(file, std::ios::binary);
		if (!out)
		{
			return false;
		}

		if (size)
		{
			out.write(reinterpret_cast<const char*>(data), size);
		}

		return static_cast<bool>(out);
	}
}
