#include "Application/Platform/Windows/Win32Shell.h"

#include <format>
#include <system_error>

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

namespace gglab::win32
{
	bool OpenDirectory(const std::filesystem::path& directory, std::string& failure) noexcept
	{
		failure.clear();
		if (directory.empty() || directory.native().find(L'\0') != std::wstring::npos)
		{
			failure = "The output directory is empty or contains an embedded null character.";
			return false;
		}

		std::error_code error;
		const std::filesystem::path absoluteDirectory = std::filesystem::absolute(directory, error);
		if (error)
		{
			failure = std::format("Could not resolve the output directory (error {}).", error.value());
			return false;
		}
		if (!std::filesystem::is_directory(absoluteDirectory, error))
		{
			failure = error
				? std::format("Could not access the output directory (error {}).", error.value())
				: "The output directory no longer exists or is not a directory.";
			return false;
		}

		// Shell extensions may use COM. Balance only this call's initialization;
		// an existing apartment remains unchanged, including a different mode.
		const HRESULT comResult = ::CoInitializeEx(nullptr,
			COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
		{
			failure = std::format("Could not initialize the Windows Shell apartment (HRESULT 0x{:08X}).",
				static_cast<unsigned long>(comResult));
			return false;
		}
		SHELLEXECUTEINFOW execute{};
		execute.cbSize = sizeof(execute);
		// Complete the launch before leaving this scoped apartment. This does not
		// wait for Explorer to exit. Report errors in our panel, not a Shell dialog.
		execute.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
		execute.lpVerb = L"open";
		execute.lpFile = absoluteDirectory.c_str();
		execute.nShow = SW_SHOWNORMAL;
		const bool accepted = ::ShellExecuteExW(&execute) != FALSE;
		const DWORD shellError = accepted ? ERROR_SUCCESS : ::GetLastError();
		if (SUCCEEDED(comResult))
		{
			::CoUninitialize();
		}
		if (!accepted)
		{
			failure = std::format("Windows Shell could not open the output directory (error {}).", shellError);
		}
		return accepted;
	}
}
