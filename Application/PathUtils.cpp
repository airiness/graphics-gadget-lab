#include "Precompiled.h"
#include "PathUtils.h"

namespace gglab::utils
{
	std::filesystem::path Canonical(const std::filesystem::path& path) noexcept
	{
		std::error_code errorCode;
		auto canonical = std::filesystem::weakly_canonical(path, errorCode);
		return (errorCode ? path.lexically_normal() : canonical).make_preferred();
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

		return std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
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
