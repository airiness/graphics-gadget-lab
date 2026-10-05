#include "Application/Platform/Windows/Win32UnattendedProcess.h"

#include <Windows.h>
#include <crtdbg.h>
#include <stdlib.h>

#include <array>

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
}
