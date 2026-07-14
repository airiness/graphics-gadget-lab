#include "Core/Precompiled.h"
#include "Core/Async/ProgressChannel.h"

namespace gglab
{
	void ProgressChannel::Report(
		float fraction,
		std::string_view stage,
		std::string_view detail,
		uint32_t completedUnits,
		uint32_t totalUnits) noexcept
	{
		try
		{
			const std::scoped_lock lock(m_Mutex);
			m_Snapshot.m_Fraction = std::max(
				m_Snapshot.m_Fraction,
				std::clamp(fraction, 0.0f, 1.0f));
			m_Snapshot.m_Stage = stage;
			m_Snapshot.m_Detail = detail;
			m_Snapshot.m_CompletedUnits = completedUnits;
			m_Snapshot.m_TotalUnits = totalUnits;
			++m_Snapshot.m_Revision;
		}
		catch (...)
		{
			// Progress reporting is diagnostic and must never fail the operation.
		}
	}

	ProgressSnapshot ProgressChannel::GetSnapshot() const noexcept
	{
		try
		{
			const std::scoped_lock lock(m_Mutex);
			return m_Snapshot;
		}
		catch (...)
		{
			return {};
		}
	}

	ProgressReporter::ProgressReporter(
		ProgressChannelPtr channel,
		float begin,
		float end) noexcept :
		m_Channel(std::move(channel)),
		m_Begin(std::clamp(begin, 0.0f, 1.0f)),
		m_End(std::clamp(end, 0.0f, 1.0f))
	{
		if (m_End < m_Begin)
		{
			std::swap(m_Begin, m_End);
		}
	}

	void ProgressReporter::Report(
		float fraction,
		std::string_view stage,
		std::string_view detail,
		uint32_t completedUnits,
		uint32_t totalUnits) const noexcept
	{
		if (!m_Channel)
		{
			return;
		}

		const float localFraction = std::clamp(fraction, 0.0f, 1.0f);
		m_Channel->Report(
			std::lerp(m_Begin, m_End, localFraction),
			stage,
			detail,
			completedUnits,
			totalUnits);
	}

	ProgressReporter ProgressReporter::Subrange(float begin, float end) const noexcept
	{
		const float localBegin = std::clamp(begin, 0.0f, 1.0f);
		const float localEnd = std::clamp(end, 0.0f, 1.0f);
		return ProgressReporter(
			m_Channel,
			std::lerp(m_Begin, m_End, localBegin),
			std::lerp(m_Begin, m_End, localEnd));
	}
}
