#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gglab
{
	struct SelfTestChildResult
	{
		bool m_Completed = false;
		uint32_t m_ExitCode = 0;
		std::string m_Output;
	};

	// Runs this test executable only, with two restricted ASCII argument tokens.
	// Owns/reaps the child with a 15-second watchdog; never invokes an external tool.
	[[nodiscard]] SelfTestChildResult RunSelfTestChild(
		std::string_view mode, std::string_view scenario) noexcept;

	struct SelfTestSummary
	{
		size_t m_CheckCount = 0;
		size_t m_FailureCount = 0;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return m_CheckCount > 0 && m_FailureCount == 0;
		}
	};

	class SelfTestReporterBase
	{
	public:
		virtual ~SelfTestReporterBase() = default;

		virtual void OnSuiteStarted(std::string_view suiteName) noexcept = 0;
		virtual void OnCheckCompleted(std::string_view checkName, bool succeeded) noexcept = 0;
		virtual void OnSuiteFinished(std::string_view suiteName, const SelfTestSummary& summary) noexcept = 0;
	};

	class SelfTestContext final
	{
	public:
		explicit SelfTestContext(SelfTestReporterBase& reporter) noexcept;

		void Check(bool condition, std::string_view name) noexcept;
		[[nodiscard]] const SelfTestSummary& GetSummary() const noexcept;

	private:
		SelfTestReporterBase& m_Reporter;
		SelfTestSummary m_Summary{};
	};

	using SelfTestSuiteFunction = void (*)(SelfTestContext& context) noexcept;

	struct SelfTestSuiteDesc
	{
		std::string_view m_Id;
		SelfTestSuiteFunction m_Run = nullptr;

		[[nodiscard]] bool IsValid() const noexcept { return !m_Id.empty() && m_Run != nullptr; }
	};

	class ConsoleSelfTestReporter final : public SelfTestReporterBase
	{
	public:
		void OnSuiteStarted(std::string_view suiteName) noexcept override;
		void OnCheckCompleted(std::string_view checkName, bool succeeded) noexcept override;
		void OnSuiteFinished(std::string_view suiteName, const SelfTestSummary& summary) noexcept override;
	};

	[[nodiscard]] bool RunSelfTestSuite(const SelfTestSuiteDesc& suite, SelfTestReporterBase& reporter) noexcept;
}
