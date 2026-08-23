#pragma once

namespace gglab::develop_gui
{
	[[nodiscard]] constexpr const char* BoolToYesNo(bool value) noexcept
	{
		return value ? "Yes" : "No";
	}
}
