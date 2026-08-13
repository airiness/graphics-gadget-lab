#include "Core/Platform/Win/Win32ProcessUtils.h"

#include <Windows.h>

namespace gglab::win32
{
	uint32_t GetCurrentProcessId() noexcept
	{
		return ::GetCurrentProcessId();
	}

	uint64_t GetTickCount64() noexcept
	{
		return ::GetTickCount64();
	}
}
