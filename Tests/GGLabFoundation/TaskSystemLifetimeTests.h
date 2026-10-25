#pragma once

#include <string_view>

namespace gglab::foundation::tests
{
	// --task-lifetime-child <case> is private infrastructure, not a public suite selector.
	[[nodiscard]] int RunTaskSystemLifetimeChild(std::string_view scenario) noexcept;
	// The normal test entry runs each case in its own hidden child with a 15-second
	// watchdog. Expected rejection requires abnormal exit and the exact diagnostic reason.
	[[nodiscard]] bool RunTaskSystemLifetimeTests() noexcept;
}
