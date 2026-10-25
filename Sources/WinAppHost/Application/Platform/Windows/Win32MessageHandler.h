#pragma once

#include <windows.h>

namespace gglab
{
	struct Win32MessageResult
	{
		bool m_Handled = false;
		LRESULT m_Result = 0;
	};

	class Win32MessageHandler
	{
	public:
		virtual ~Win32MessageHandler() = default;

		[[nodiscard]] virtual Win32MessageResult HandleWin32Message(
			HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept = 0;
	};
}
