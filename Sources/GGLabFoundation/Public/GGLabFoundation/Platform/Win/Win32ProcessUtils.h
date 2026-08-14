#pragma once

#include <cstdint>

namespace gglab::win32
{
	[[nodiscard]] std::uint32_t GetCurrentProcessId() noexcept;
	[[nodiscard]] std::uint64_t GetTickCount64() noexcept;
}
