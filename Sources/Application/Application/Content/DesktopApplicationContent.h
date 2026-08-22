#pragma once
#include "Application/Content/ApplicationContentRegistration.h"

#include <string_view>

namespace gglab
{
	inline constexpr std::string_view DesktopStartDemoId = "Demo.Start";
	inline constexpr std::string_view DesktopPlaygroundDemoId = "Demo.Playground";
	inline constexpr std::string_view DesktopLabHostDemoId = "Demo.LabHost";
	inline constexpr std::string_view DesktopDefaultLabId = "gglab.lab.culling";

	[[nodiscard]] ApplicationContentRegistration CreateDesktopApplicationContent() noexcept;
}
