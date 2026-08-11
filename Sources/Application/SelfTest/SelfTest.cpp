#include "Application/SelfTest/SelfTest.h"

#include <cstdio>

namespace gglab
{
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
		std::printf("Running headless %.*s self-tests...\n", static_cast<int>(suiteName.size()),
			suiteName.data());
	}

	void ConsoleSelfTestReporter::OnCheckCompleted(
		std::string_view checkName, bool succeeded) noexcept
	{
		FILE* output = succeeded ? stdout : stderr;
		std::fprintf(output, "[%s] %.*s\n", succeeded ? "PASS" : "FAIL",
			static_cast<int>(checkName.size()), checkName.data());
	}

	void ConsoleSelfTestReporter::OnSuiteFinished(
		std::string_view suiteName, const SelfTestSummary& summary) noexcept
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
