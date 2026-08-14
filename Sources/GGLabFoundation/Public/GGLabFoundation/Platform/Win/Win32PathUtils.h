#pragma once

#include <filesystem>

namespace gglab::win32
{
	[[nodiscard]] std::filesystem::path GetExecutableDirectory() noexcept;
}
