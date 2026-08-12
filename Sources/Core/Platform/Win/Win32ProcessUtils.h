#pragma once

#include <cstdint>

namespace gglab::win32
{
	[[nodiscard]] uint32_t GetCurrentProcessId() noexcept;
	[[nodiscard]] uint64_t GetTickCount64() noexcept;
}
