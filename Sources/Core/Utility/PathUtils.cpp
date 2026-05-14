#include "Core/Precompiled.h"
#include "Core/Utility/PathUtils.h"

namespace gglab::utils
{
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

	bool LinkDirectory(const std::filesystem::path& srcDir, const std::filesystem::path& dstDir) noexcept
	{
		std::error_code errorCode;
		if (!std::filesystem::exists(srcDir, errorCode))
		{
			return false;
		}

		std::filesystem::remove(dstDir, errorCode);
		std::filesystem::remove_all(dstDir, errorCode);

		if (CreateSymbolicLinkW(dstDir.c_str(), srcDir.c_str(),
			SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
		{
			return true;
		}

		return false;
	}

	std::filesystem::path GetModuleDirectory(HMODULE hModule) noexcept
	{
		std::wstring path;
		DWORD size = MAX_PATH;
		for (;;)
		{
			path.resize(size);
			DWORD name = ::GetModuleFileNameW(hModule, path.data(), size);
			if (name == 0)
			{
				return std::filesystem::path{ L"." };
			}

			if (name < size - 1)
			{
				path.resize(name);
				break;
			}
			size *= 2;
		}
		return std::filesystem::path{ path }.parent_path();
	}

	std::filesystem::path GetExeOutDir() noexcept
	{
		return GetModuleDirectory(nullptr);
	}
}
