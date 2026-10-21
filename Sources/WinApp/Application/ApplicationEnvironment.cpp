#include "Application/ApplicationEnvironment.h"
// Generated before compilation by PropertySheets/GGLabBuildStamp.targets.
#include "GGLabBuildStamp.g.h"

#include <format>
#include <optional>
#include <string>

#include <windows.h>

namespace gglab
{
	namespace
	{
		// RtlGetVersion reports the running version regardless of the executable
		// manifest's compatibility section, unlike GetVersionEx.
		std::string QueryOperatingSystem() noexcept
		{
			using RtlGetVersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
			const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
			const auto getVersion = ntdll ?
				reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion")) :
				nullptr;
			OSVERSIONINFOW version{ .dwOSVersionInfoSize = sizeof(OSVERSIONINFOW) };
			if (!getVersion || getVersion(&version) != 0)
			{
				return {};
			}
			return std::format("Windows {}.{}.{}",
				version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);
		}

		std::string GetCompiler() noexcept
		{
			// _MSC_FULL_VER packs major, minor and build as MMmmbbbbb.
			return std::format("MSVC {}.{}.{}",
				_MSC_FULL_VER / 10000000, (_MSC_FULL_VER / 100000) % 100, _MSC_FULL_VER % 100000);
		}
	}

	CaptureHostEnvironment MakeApplicationHostEnvironment(bool hidden) noexcept
	{
		constexpr int Dirty = GGLAB_BUILD_STAMP_DIRTY;
		CaptureHostEnvironment environment;
		environment.m_Build = {
			.m_Available = GGLAB_BUILD_STAMP_AVAILABLE != 0,
			.m_Revision = GGLAB_BUILD_STAMP_REVISION,
			.m_Dirty = Dirty < 0 ? std::nullopt : std::optional<bool>(Dirty != 0),
			.m_Configuration = GGLAB_BUILD_STAMP_CONFIGURATION,
			.m_Compiler = GetCompiler(),
		};
		environment.m_OperatingSystem = QueryOperatingSystem();
		environment.m_Presentation = hidden ? "hidden" : "windowed";
		return environment;
	}
}
