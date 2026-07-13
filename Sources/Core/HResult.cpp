#include "Core/Precompiled.h"
#include "Core/HResult.h"
#include "Core/Platform/Win/Win32DiagnosticOutput.h"
#include "Core/Utility/StringUtils.h"

namespace gglab
{
	namespace
	{
		std::string HexHr(HRESULT hr)
		{
			std::ostringstream os;
			os << "0x" << std::uppercase << std::hex
				<< std::setw(8) << std::setfill('0') << static_cast<unsigned>(hr);
			return os.str();
		}

	}

	std::string FormatHResult(HRESULT hr)
	{
		_com_error e(hr);
		std::string msg = utils::ToString(e.ErrorMessage());
		if (msg.empty())
		{
			return HexHr(hr);
		}
		std::ostringstream os;
		os << HexHr(hr) << " (" << msg << ")";
		return os.str();
	}

	bool IsDeviceRemovedHResult(HRESULT hr) noexcept
	{
		return hr == DXGI_ERROR_DEVICE_REMOVED
			|| hr == DXGI_ERROR_DEVICE_HUNG
			|| hr == DXGI_ERROR_DEVICE_RESET;
	}

	void ReportAndAbort(
		HRESULT hr,
		std::string_view context,
		std::source_location loc) noexcept
	{
		std::ostringstream os;

		os << "=== FATAL: HRESULT Failure ===\n";
		os << "Time   : " << win32::FormatLocalTime() << "\n";
		os << "Thread : " << ::GetCurrentThreadId() << "\n";
		os << "Where  : " << loc.file_name() << ":" << loc.line()
			<< " (" << loc.function_name() << ")\n";
		if (!context.empty())
		{
			os << "Expr   : " << context << "\n";
		}

		os << "Result : " << FormatHResult(hr) << "\n";

		os << "Action : Abort immediately.\n"
			"==============================";

		const std::string text = os.str();
		win32::WriteDiagnosticOutput(text);

#if defined(BUILD_DEBUG)
		__debugbreak();
#endif
		std::abort();
	}
}
