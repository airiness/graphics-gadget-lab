#include "GGLabFoundation/Platform/Win/Win32ProcessUtils.h"

#include <Windows.h>

namespace gglab::win32
{
	std::uint32_t GetCurrentProcessId() noexcept
	{
		return ::GetCurrentProcessId();
	}

	std::uint64_t GetTickCount64() noexcept
	{
		return ::GetTickCount64();
	}
}
