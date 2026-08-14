#pragma once

#include <string>

namespace gglab::win32
{
	[[nodiscard]] std::string FormatLocalTime();
	void WriteDiagnosticOutput(const std::string& text) noexcept;
}
