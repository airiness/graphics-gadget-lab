#include "Application/Platform/Windows/Win32UnattendedProcess.h"
#include "AppRuntimeLog.h"

#include <Windows.h>
#include <crtdbg.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>

#include <array>
#include <cstdint>
#include <cstdio>

namespace gglab::win32
{
	void ConfigureUnattendedFailureReporting() noexcept
	{
		::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
		// assert() and abort() report to stderr instead of a message box, and
		// abort() does not invoke Windows Error Reporting.
		_set_error_mode(_OUT_TO_STDERR);
		_set_abort_behavior(0, _CALL_REPORTFAULT);
#if defined(_DEBUG)
		constexpr std::array reportTypes{ _CRT_WARN, _CRT_ERROR, _CRT_ASSERT };
		for (const int reportType : reportTypes)
		{
			_CrtSetReportMode(reportType, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
			_CrtSetReportFile(reportType, _CRTDBG_FILE_STDERR);
		}
#endif
	}

	bool RedirectStandardOutputToFile(const std::filesystem::path& path) noexcept
	{
		// Shared read access lets a launcher follow the log while it is written.
		const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			return false;
		}
		const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(file), _O_WRONLY);
		if (descriptor < 0)
		{
			::CloseHandle(file);
			return false;
		}
		std::fflush(stdout);
		std::fflush(stderr);
		const bool redirected = _dup2(descriptor, _fileno(stdout)) == 0 &&
			_dup2(descriptor, _fileno(stderr)) == 0;
		_close(descriptor);
		if (!redirected)
		{
			return false;
		}
		// Unbuffered, so outcome lines are visible to a polling launcher at once.
		std::setvbuf(stdout, nullptr, _IONBF, 0);
		std::setvbuf(stderr, nullptr, _IONBF, 0);
		const auto output = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdout)));
		const auto error = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr)));
		return output != INVALID_HANDLE_VALUE && error != INVALID_HANDLE_VALUE &&
			::SetStdHandle(STD_OUTPUT_HANDLE, output) && ::SetStdHandle(STD_ERROR_HANDLE, error);
	}

	void LogConsoleControlEvents() noexcept
	{
		::SetConsoleCtrlHandler([](DWORD controlType) noexcept -> BOOL
			{
				const char* name = "unknown";
				switch (controlType)
				{
				case CTRL_C_EVENT:
					name = "Ctrl+C";
					break;
				case CTRL_BREAK_EVENT:
					name = "Ctrl+Break";
					break;
				case CTRL_CLOSE_EVENT:
					name = "console close";
					break;
				case CTRL_LOGOFF_EVENT:
					name = "logoff";
					break;
				case CTRL_SHUTDOWN_EVENT:
					name = "shutdown";
					break;
				default:
					break;
				}
				GGLAB_LOG_WARN_ALWAYS("Console control event received: {}.", name);
				// The default handler still ends the process.
				return FALSE;
			}, TRUE);
	}
}
