#pragma once

#include <cstddef>
#include <string_view>

namespace gglab
{
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
		virtual void OnCheckCompleted(
			std::string_view checkName,
			bool succeeded) noexcept = 0;
		virtual void OnSuiteFinished(
			std::string_view suiteName,
			const SelfTestSummary& summary) noexcept = 0;
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

		[[nodiscard]] bool IsValid() const noexcept
		{
			return !m_Id.empty() && m_Run != nullptr;
		}
	};

	class ConsoleSelfTestReporter final : public SelfTestReporterBase
	{
	public:
		void OnSuiteStarted(std::string_view suiteName) noexcept override;
		void OnCheckCompleted(
			std::string_view checkName,
			bool succeeded) noexcept override;
		void OnSuiteFinished(
			std::string_view suiteName,
			const SelfTestSummary& summary) noexcept override;
	};

	[[nodiscard]] bool RunSelfTestSuite(
		const SelfTestSuiteDesc& suite,
		SelfTestReporterBase& reporter) noexcept;
}
