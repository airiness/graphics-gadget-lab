#include "ProductRuntimeInspection.h"

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iterator>
#include <system_error>

namespace gglab
{
	namespace
	{
		class ProcessHandle
		{
		public:
			explicit ProcessHandle(HANDLE handle) noexcept : m_Handle(handle) {}
			~ProcessHandle()
			{
				if (m_Handle && m_Handle != INVALID_HANDLE_VALUE)
				{
					CloseHandle(m_Handle);
				}
			}
			ProcessHandle(const ProcessHandle&) = delete;
			ProcessHandle& operator=(const ProcessHandle&) = delete;
			HANDLE Get() const noexcept { return m_Handle; }

		private:
			HANDLE m_Handle;
		};

		struct CaptureFile
		{
			std::filesystem::path m_Path;
			~CaptureFile()
			{
				std::error_code ignored;
				std::filesystem::remove(m_Path, ignored);
			}
		};
	}

	ProductInspectionResult InspectProductRuntime(const std::filesystem::path& application,
		const std::wstring& options) noexcept
	{
		ProductInspectionResult result;
		try
		{
			CaptureFile output{ std::filesystem::temp_directory_path() /
				("gglab-runtime-inspection-" + std::to_string(GetCurrentProcessId()) + "-" +
					std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".log") };
			{
				SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
				ProcessHandle capture(CreateFileW(output.m_Path.c_str(), GENERIC_WRITE,
					FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
				ProcessHandle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
					&security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
				if (capture.Get() == INVALID_HANDLE_VALUE || input.Get() == INVALID_HANDLE_VALUE)
				{
					return result;
				}
				STARTUPINFOW startup{};
				startup.cb = sizeof(startup);
				startup.dwFlags = STARTF_USESTDHANDLES;
				startup.hStdInput = input.Get();
				startup.hStdOutput = capture.Get();
				startup.hStdError = capture.Get();
				PROCESS_INFORMATION process{};
				std::wstring command = L"\"" + application.wstring() + L"\" --inspect-runtime " + options;
				const auto directory = std::filesystem::current_path();
				if (CreateProcessW(application.c_str(), command.data(), nullptr, nullptr, TRUE,
					CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process))
				{
					ProcessHandle owner(process.hProcess);
					ProcessHandle thread(process.hThread);
					result.m_Completed = WaitForSingleObject(owner.Get(), 15000) == WAIT_OBJECT_0;
					if (!result.m_Completed)
					{
						TerminateProcess(owner.Get(), 1);
						WaitForSingleObject(owner.Get(), 5000);
					}
					DWORD exitCode = 0;
					GetExitCodeProcess(owner.Get(), &exitCode);
					result.m_ExitCode = exitCode;
				}
			}
			std::ifstream stream(output.m_Path, std::ios::binary);
			result.m_Output.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
			if (!result.m_Completed || result.m_ExitCode != 0)
			{
				return result;
			}
			const auto snapshot = nlohmann::json::parse(result.m_Output);
			if (snapshot.at("schemaVersion") != 1 || snapshot.at("kind") != "gglab.runtime-inspection")
			{
				return result;
			}
			const auto& paths = snapshot.at("paths");
			const auto path = [&](const char* key)
				{
					const auto utf8 = paths.at(key).get<std::string>();
					return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
				};
			const std::string backend = snapshot.at("backend");
			if (backend != "dx12" && backend != "vulkan")
			{
				return result;
			}
			result.m_Backend = backend == "vulkan" ? RHIBackendType::Vulkan : RHIBackendType::DX12;
			result.m_ArtifactOnly = snapshot.at("artifactOnly").get<bool>();
			result.m_Paths = {
				.m_RuntimeRoot = path("runtimeRoot"),
				.m_AssetRoot = path("assetRoot"),
				.m_ShaderArtifactRoot = path("shaderArtifactRoot"),
				.m_IblDerivedDataRoot = path("iblDerivedDataRoot"),
				.m_TextureDerivedDataRoot = path("textureDerivedDataRoot"),
				.m_EnvironmentAssetRoot = path("environmentAssetRoot"),
				.m_SettingsRoot = path("settingsRoot"),
				.m_CaptureRoot = path("captureRoot"),
			};
			result.m_Valid = result.m_Paths.IsValid();
		}
		catch (...)
		{
			result.m_Valid = false;
		}
		return result;
	}
}
