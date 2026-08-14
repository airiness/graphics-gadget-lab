#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"

#include <Windows.h>

#include <limits>
#include <string>

namespace gglab::win32
{
	std::filesystem::path GetExecutableDirectory() noexcept
	{
		std::wstring path;
		DWORD size = MAX_PATH;
		for (;;)
		{
			path.resize(size);
			const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), size);
			if (length == 0)
			{
				return std::filesystem::path{ L"." };
			}
			if (length < size - 1)
			{
				path.resize(length);
				return std::filesystem::path{ path }.parent_path();
			}
			if (size > std::numeric_limits<DWORD>::max() / 2)
			{
				return std::filesystem::path{ L"." };
			}
			size *= 2;
		}
	}
}
