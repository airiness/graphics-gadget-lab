#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace gglab
{
	struct ProgressSnapshot
	{
		float m_Fraction = 0.0f;
		std::string m_Stage;
		std::string m_Detail;
		uint32_t m_CompletedUnits = 0;
		uint32_t m_TotalUnits = 0;
		uint64_t m_Revision = 0;

		[[nodiscard]] bool HasProgress() const noexcept { return m_Revision != 0; }
	};

	class ProgressChannel final
	{
	public:
		void Report(float fraction, std::string_view stage, std::string_view detail = {},
			uint32_t completedUnits = 0, uint32_t totalUnits = 0) noexcept;
		[[nodiscard]] ProgressSnapshot GetSnapshot() const noexcept;

	private:
		mutable std::mutex m_Mutex;
		ProgressSnapshot m_Snapshot;
	};

	using ProgressChannelPtr = std::shared_ptr<ProgressChannel>;

	// Maps local progress in [0, 1] into a sub-range of a shared operation.
	// Reporters are cheap value types and keep the channel alive across worker jobs.
	class ProgressReporter final
	{
	public:
		ProgressReporter() noexcept = default;
		explicit ProgressReporter(
			ProgressChannelPtr channel, float begin = 0.0f, float end = 1.0f) noexcept;

		void Report(float fraction, std::string_view stage, std::string_view detail = {},
			uint32_t completedUnits = 0, uint32_t totalUnits = 0) const noexcept;
		[[nodiscard]] ProgressReporter Subrange(float begin, float end) const noexcept;
		[[nodiscard]] const ProgressChannelPtr& GetChannel() const noexcept { return m_Channel; }
		explicit operator bool() const noexcept { return m_Channel != nullptr; }

	private:
		ProgressChannelPtr m_Channel;
		float m_Begin = 0.0f;
		float m_End = 1.0f;
	};
}
