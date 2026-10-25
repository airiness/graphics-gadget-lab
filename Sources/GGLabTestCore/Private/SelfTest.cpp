#include "GGLabTestCore/SelfTest.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <initializer_list>
#include <memory>

namespace gglab
{
	SelfTestChildResult RunSelfTestChild(std::string_view mode, std::string_view scenario) noexcept
	{
		SelfTestChildResult result;
		std::array<wchar_t, 32768> executable{};
		const DWORD pathLength =
			GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
		if (pathLength == 0 || pathLength >= executable.size())
		{
			return result;
		}
		std::wstring command = L"\"" + std::wstring(executable.data()) + L"\"";
		for (const auto token : { mode, scenario })
		{
			if (token.empty())
			{
				return result;
			}
			command += L' ';
			for (const char character : token)
			{
				if (!((character >= 'a' && character <= 'z') ||
					(character >= '0' && character <= '9') || character == '-'))
				{
					return result;
				}
				command += static_cast<wchar_t>(character);
			}
		}
		SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
		HANDLE read = nullptr;
		HANDLE write = nullptr;
		if (!CreatePipe(&read, &write, &security, 65536))
		{
			return result;
		}
		using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;
		const Handle reader(read, &CloseHandle);
		Handle writer(write, &CloseHandle);
		if (!SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0))
		{
			return result;
		}
		const HANDLE inputHandle = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			&security, OPEN_EXISTING, 0, nullptr);
		if (inputHandle == INVALID_HANDLE_VALUE)
		{
			return result;
		}
		const Handle input(inputHandle, &CloseHandle);
		STARTUPINFOW startup{ sizeof(startup) };
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdInput = input.get();
		startup.hStdOutput = write;
		startup.hStdError = write;
		PROCESS_INFORMATION process{};
		if (!CreateProcessW(executable.data(), command.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
		{
			return result;
		}
		const Handle child(process.hProcess, &CloseHandle);
		const Handle thread(process.hThread, &CloseHandle);
		writer.reset();
		const DWORD wait = WaitForSingleObject(child.get(), 15000);
		if (wait != WAIT_OBJECT_0)
		{
			if (!TerminateProcess(child.get(), 125) ||
				WaitForSingleObject(child.get(), 5000) != WAIT_OBJECT_0)
			{
				// Never block reading a pipe while its writer might still be alive.
				return result;
			}
		}
		DWORD exitCode = 0;
		if (!GetExitCodeProcess(child.get(), &exitCode))
		{
			return result;
		}
		result.m_Completed = wait == WAIT_OBJECT_0;
		result.m_ExitCode = exitCode;
		std::array<char, 4096> buffer{};
		DWORD count = 0;
		while (ReadFile(read, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) && count)
		{
			result.m_Output.append(buffer.data(), count);
		}
		return result;
	}

	SelfTestContext::SelfTestContext(SelfTestReporterBase& reporter) noexcept : m_Reporter(reporter)
	{
	}

	void SelfTestContext::Check(bool condition, std::string_view name) noexcept
	{
		++m_Summary.m_CheckCount;
		if (!condition)
		{
			++m_Summary.m_FailureCount;
		}
		m_Reporter.OnCheckCompleted(name, condition);
	}

	const SelfTestSummary& SelfTestContext::GetSummary() const noexcept
	{
		return m_Summary;
	}

	void ConsoleSelfTestReporter::OnSuiteStarted(std::string_view suiteName) noexcept
	{
		std::printf("Running headless %.*s self-tests...\n", static_cast<int>(suiteName.size()), suiteName.data());
	}

	void ConsoleSelfTestReporter::OnCheckCompleted(std::string_view checkName, bool succeeded) noexcept
	{
		FILE* output = succeeded ? stdout : stderr;
		std::fprintf(output, "[%s] %.*s\n", succeeded ? "PASS" : "FAIL", static_cast<int>(checkName.size()), checkName.data());
	}

	void ConsoleSelfTestReporter::OnSuiteFinished(std::string_view suiteName, const SelfTestSummary& summary) noexcept
	{
		FILE* output = summary.Succeeded() ? stdout : stderr;
		if (summary.Succeeded())
		{
			std::fprintf(output, "%.*s self-tests passed (%zu checks).\n",
				static_cast<int>(suiteName.size()), suiteName.data(), summary.m_CheckCount);
			return;
		}
		if (summary.m_CheckCount == 0)
		{
			std::fprintf(output, "%.*s self-tests failed (no checks were executed).\n",
				static_cast<int>(suiteName.size()), suiteName.data());
			return;
		}

		std::fprintf(output, "%.*s self-tests failed (%zu of %zu checks failed).\n",
			static_cast<int>(suiteName.size()), suiteName.data(), summary.m_FailureCount,
			summary.m_CheckCount);
	}

	bool RunSelfTestSuite(const SelfTestSuiteDesc& suite, SelfTestReporterBase& reporter) noexcept
	{
		if (!suite.IsValid())
		{
			return false;
		}

		reporter.OnSuiteStarted(suite.m_Id);
		SelfTestContext context(reporter);
		suite.m_Run(context);
		reporter.OnSuiteFinished(suite.m_Id, context.GetSummary());
		return context.GetSummary().Succeeded();
	}
}
