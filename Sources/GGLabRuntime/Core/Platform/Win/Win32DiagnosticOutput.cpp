#include "Core/Platform/Win/Win32DiagnosticOutput.h"

#include <Windows.h>

#include <chrono>
#include <cstdio>
#include <ctime>

namespace gglab::win32
{
	std::string FormatLocalTime()
	{
		using namespace std::chrono;
		const auto now = system_clock::now();
		const std::time_t time = system_clock::to_time_t(now);
		std::tm localTime{};
		::localtime_s(&localTime, &time);

		char buffer[64]{};
		std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
			localTime.tm_year + 1900,
			localTime.tm_mon + 1,
			localTime.tm_mday,
			localTime.tm_hour,
			localTime.tm_min,
			localTime.tm_sec);
		return buffer;
	}

	void WriteDiagnosticOutput(const std::string& text) noexcept
	{
		::OutputDebugStringA(text.c_str());
		::OutputDebugStringA("\n");
		std::fputs(text.c_str(), stderr);
		std::fputc('\n', stderr);
		std::fflush(stderr);
	}
}
