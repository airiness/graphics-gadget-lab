#pragma once

#include <cstddef>
#include <cstdio>
#include <string_view>

namespace napa::voxel::testing
{
	struct TestSummary
	{
		size_t m_CheckCount = 0;
		size_t m_FailureCount = 0;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return m_CheckCount > 0 && m_FailureCount == 0;
		}
	};

	class TestReporterBase
	{
	public:
		virtual ~TestReporterBase() = default;

		virtual void OnSuiteStarted(std::string_view suiteName) noexcept = 0;
		virtual void OnCheckCompleted(std::string_view checkName, bool succeeded) noexcept = 0;
		virtual void OnSuiteFinished(
			std::string_view suiteName, const TestSummary& summary) noexcept = 0;
	};

	class TestContext final
	{
	public:
		explicit TestContext(TestReporterBase& reporter) noexcept;

		void Check(bool condition, std::string_view name) noexcept;
		[[nodiscard]] const TestSummary& GetSummary() const noexcept;

	private:
		TestReporterBase& m_Reporter;
		TestSummary m_Summary{};
	};

	using TestSuiteFunction = void (*)(TestContext& context) noexcept;

	struct TestSuiteDesc
	{
		std::string_view m_Id;
		TestSuiteFunction m_Run = nullptr;

		[[nodiscard]] bool IsValid() const noexcept { return !m_Id.empty() && m_Run != nullptr; }
	};

	class ConsoleTestReporter final : public TestReporterBase
	{
	public:
		void OnSuiteStarted(std::string_view suiteName) noexcept override;
		void OnCheckCompleted(std::string_view checkName, bool succeeded) noexcept override;
		void OnSuiteFinished(
			std::string_view suiteName, const TestSummary& summary) noexcept override;
	};

	[[nodiscard]] bool RunTestSuite(
		const TestSuiteDesc& suite, TestReporterBase& reporter) noexcept;
}
