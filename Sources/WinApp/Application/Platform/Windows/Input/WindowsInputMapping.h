#pragma once

#include "ApplicationInput.h"

#include <cstdint>
#include <span>

namespace gglab
{
	struct WindowsKeyMapping
	{
		uint32_t m_VirtualKey = 0;
		AppInputKey m_Key = AppInputKey::Count;
	};

	[[nodiscard]] std::span<const WindowsKeyMapping> GetWindowsKeyMappings() noexcept;
	[[nodiscard]] AppInputKey MapWindowsVirtualKey(uint32_t virtualKey) noexcept;
}
