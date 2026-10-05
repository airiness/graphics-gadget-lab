#include "Application/Capture/ApplicationFrameCapture.h"
#include "AppRuntimeLog.h"

#include <utility>
#include <vector>

namespace gglab
{
	ApplicationFrameCapture::ApplicationFrameCapture(FrameCaptureCoordinator& coordinator,
		std::filesystem::path defaultOutputDirectory) noexcept :
		m_Coordinator(&coordinator),
		m_DefaultOutputDirectory(std::move(defaultOutputDirectory))
	{
	}

	uint64_t ApplicationFrameCapture::Capture() noexcept
	{
		const bool afterReady = m_Settings.m_Timing == FrameCaptureTiming::AfterReady;
		const uint64_t requestId = m_Coordinator->Submit({
			.m_Source = m_Settings.m_Source,
			.m_Timing = m_Settings.m_Timing,
			.m_SettleFrames = afterReady ? m_Settings.m_SettleFrames : 0,
			.m_OutputDirectory = m_Settings.m_OutputDirectory,
			.m_Label = m_Settings.m_Label,
			.m_Note = m_Settings.m_Note,
			});
		GGLAB_LOG_INFO_ALWAYS("Frame capture {} requested (source={}, timing={}).", requestId,
			GetFrameCaptureSourceName(m_Settings.m_Source),
			GetFrameCaptureTimingName(m_Settings.m_Timing));
		return requestId;
	}

	void ApplicationFrameCapture::Update() noexcept
	{
		std::vector<FrameCaptureRequestResult> results;
		m_Coordinator->ConsumeResults(results);
		for (FrameCaptureRequestResult& result : results)
		{
			if (result.m_Status == FrameCaptureRequestStatus::Completed)
			{
				GGLAB_LOG_INFO_ALWAYS("Frame capture {} written to '{}'.", result.m_RequestId,
					result.m_ImagePath.string());
			}
			else
			{
				GGLAB_LOG_WARN_ALWAYS("Frame capture {} {}: {}", result.m_RequestId,
					result.m_Status == FrameCaptureRequestStatus::Cancelled ? "cancelled"
					: "failed",
					result.m_Failure);
			}
			m_History.push_back({
				.m_Result = std::move(result),
				.m_FinishedAt = std::chrono::steady_clock::now(),
				});
			if (m_History.size() > MaxHistoryEntries)
			{
				m_History.pop_front();
			}
		}
	}

	uint32_t ApplicationFrameCapture::GetUnfinishedRequestCount() const noexcept
	{
		return m_Coordinator->GetUnfinishedRequestCount();
	}

	const FrameCaptureFrameState* ApplicationFrameCapture::GetLastFrameState() const noexcept
	{
		return m_Coordinator->GetLastFrameState();
	}

	uint32_t ApplicationFrameCapture::GetSettledFrameCount() const noexcept
	{
		return m_Coordinator->GetSettledFrameCount();
	}
}
