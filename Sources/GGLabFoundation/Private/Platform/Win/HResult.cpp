#include "GGLabFoundation/Platform/Win/HResult.h"
#include "GGLabFoundation/Platform/Win/Win32DiagnosticOutput.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"

#include <intrin.h>

#include <array>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace gglab
{
	namespace
	{
		[[nodiscard]] std::string HexHResult(HRESULT result)
		{
			std::ostringstream output;
			output << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
				<< static_cast<unsigned>(result);
			return output.str();
		}
	}

	std::string FormatHResult(HRESULT result)
	{
		std::array<wchar_t, 1024> buffer{};
		const DWORD length = ::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, static_cast<DWORD>(result), 0, buffer.data(),
			static_cast<DWORD>(buffer.size()), nullptr);
		std::wstring_view messageView(buffer.data(), length);
		while (!messageView.empty() &&
			(messageView.back() == L'\r' || messageView.back() == L'\n' ||
				messageView.back() == L' ' || messageView.back() == L'\t'))
		{
			messageView.remove_suffix(1);
		}
		const std::string message = utils::ToString(messageView);
		if (message.empty())
		{
			return HexHResult(result);
		}

		std::ostringstream output;
		output << HexHResult(result) << " (" << message << ")";
		return output.str();
	}

	[[noreturn]] void ReportAndAbort(
		HRESULT result, std::string_view context, std::source_location location) noexcept
	{
		std::ostringstream output;
		output << "=== FATAL: HRESULT Failure ===\n";
		output << "Time   : " << win32::FormatLocalTime() << "\n";
		output << "Thread : " << ::GetCurrentThreadId() << "\n";
		output << "Where  : " << location.file_name() << ":" << location.line() << " ("
			<< location.function_name() << ")\n";
		if (!context.empty())
		{
			output << "Expr   : " << context << "\n";
		}
		output << "Result : " << FormatHResult(result) << "\n";
		output << "Action : Abort immediately.\n==============================";

		win32::WriteDiagnosticOutput(output.str());
#if defined(BUILD_DEBUG)
		__debugbreak();
#endif
		std::abort();
	}
}
