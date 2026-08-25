#include "Core/Input/WindowsInputMapping.h"

#include <array>

namespace gglab
{
	namespace
	{
		constexpr std::array WindowsKeyMappings{
			WindowsKeyMapping{ 'A', AppInputKey::A },
			WindowsKeyMapping{ 'B', AppInputKey::B },
			WindowsKeyMapping{ 'C', AppInputKey::C },
			WindowsKeyMapping{ 'D', AppInputKey::D },
			WindowsKeyMapping{ 'E', AppInputKey::E },
			WindowsKeyMapping{ 'F', AppInputKey::F },
			WindowsKeyMapping{ 'G', AppInputKey::G },
			WindowsKeyMapping{ 'H', AppInputKey::H },
			WindowsKeyMapping{ 'I', AppInputKey::I },
			WindowsKeyMapping{ 'J', AppInputKey::J },
			WindowsKeyMapping{ 'K', AppInputKey::K },
			WindowsKeyMapping{ 'L', AppInputKey::L },
			WindowsKeyMapping{ 'M', AppInputKey::M },
			WindowsKeyMapping{ 'N', AppInputKey::N },
			WindowsKeyMapping{ 'O', AppInputKey::O },
			WindowsKeyMapping{ 'P', AppInputKey::P },
			WindowsKeyMapping{ 'Q', AppInputKey::Q },
			WindowsKeyMapping{ 'R', AppInputKey::R },
			WindowsKeyMapping{ 'S', AppInputKey::S },
			WindowsKeyMapping{ 'T', AppInputKey::T },
			WindowsKeyMapping{ 'U', AppInputKey::U },
			WindowsKeyMapping{ 'V', AppInputKey::V },
			WindowsKeyMapping{ 'W', AppInputKey::W },
			WindowsKeyMapping{ 'X', AppInputKey::X },
			WindowsKeyMapping{ 'Y', AppInputKey::Y },
			WindowsKeyMapping{ 'Z', AppInputKey::Z },
			WindowsKeyMapping{ 0x30, AppInputKey::D0 },
			WindowsKeyMapping{ 0x31, AppInputKey::D1 },
			WindowsKeyMapping{ 0x32, AppInputKey::D2 },
			WindowsKeyMapping{ 0x33, AppInputKey::D3 },
			WindowsKeyMapping{ 0x34, AppInputKey::D4 },
			WindowsKeyMapping{ 0x35, AppInputKey::D5 },
			WindowsKeyMapping{ 0x36, AppInputKey::D6 },
			WindowsKeyMapping{ 0x37, AppInputKey::D7 },
			WindowsKeyMapping{ 0x38, AppInputKey::D8 },
			WindowsKeyMapping{ 0x39, AppInputKey::D9 },
			WindowsKeyMapping{ 0x1b, AppInputKey::Escape },
			WindowsKeyMapping{ 0x20, AppInputKey::Space },
			WindowsKeyMapping{ 0xa0, AppInputKey::LeftShift },
			WindowsKeyMapping{ 0xa1, AppInputKey::RightShift },
		};
	}

	std::span<const WindowsKeyMapping> GetWindowsKeyMappings() noexcept
	{
		return WindowsKeyMappings;
	}

	AppInputKey MapWindowsVirtualKey(uint32_t virtualKey) noexcept
	{
		for (const WindowsKeyMapping& mapping : WindowsKeyMappings)
		{
			if (mapping.m_VirtualKey == virtualKey)
			{
				return mapping.m_Key;
			}
		}
		return AppInputKey::Count;
	}
}
