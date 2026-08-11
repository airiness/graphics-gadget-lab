#include "Core/Utility/PathUtils.h"

#include <Windows.h>

#include <filesystem>
#include <string>

namespace gglab::utils
{
	namespace
	{
		std::filesystem::path GetModuleDirectory(HMODULE module) noexcept
		{
			std::wstring path;
			DWORD size = MAX_PATH;
			for (;;)
			{
				path.resize(size);
				const DWORD name = ::GetModuleFileNameW(module, path.data(), size);
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
	}

	bool LinkDirectory(
		const std::filesystem::path& srcDir, const std::filesystem::path& dstDir) noexcept
	{
		std::error_code errorCode;
		if (!std::filesystem::exists(srcDir, errorCode))
		{
			return false;
		}

		std::filesystem::remove(dstDir, errorCode);
		std::filesystem::remove_all(dstDir, errorCode);

		return ::CreateSymbolicLinkW(dstDir.c_str(), srcDir.c_str(),
			SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
	}

	std::filesystem::path GetExeOutDir() noexcept
	{
		return GetModuleDirectory(nullptr);
	}
}
