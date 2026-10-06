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
		return Submit({
			.m_Source = m_Settings.m_Source,
			.m_Timing = m_Settings.m_Timing,
			.m_SettleFrames = afterReady ? m_Settings.m_SettleFrames : 0,
			.m_ReferenceViewId = m_Settings.m_ReferenceViewId,
			.m_OutputDirectory = m_Settings.m_OutputDirectory,
			.m_Label = m_Settings.m_Label,
			.m_Note = m_Settings.m_Note,
			});
	}

	uint32_t ApplicationFrameCapture::CaptureReferenceViews() noexcept
	{
		const FrameCaptureFrameState* frameState = m_Coordinator->GetLastFrameState();
		if (!frameState)
		{
			return 0;
		}
		const bool afterReady = m_Settings.m_Timing == FrameCaptureTiming::AfterReady;
		const std::vector<std::string>& viewIds = frameState->m_ReferenceViewIds;
		for (const std::string& viewId : viewIds)
		{
			Submit({
				.m_Source = m_Settings.m_Source,
				.m_Timing = m_Settings.m_Timing,
				.m_SettleFrames = afterReady ? m_Settings.m_SettleFrames : 0,
				.m_ReferenceViewId = viewId,
				.m_OutputDirectory = m_Settings.m_OutputDirectory,
				.m_Label = m_Settings.m_Label,
				.m_Note = m_Settings.m_Note,
				});
		}
		return static_cast<uint32_t>(viewIds.size());
	}

	uint64_t ApplicationFrameCapture::Submit(FrameCaptureRequest request) noexcept
	{
		const FrameCaptureSource source = request.m_Source;
		const FrameCaptureTiming timing = request.m_Timing;
		const std::string viewId = request.m_ReferenceViewId;
		const uint64_t requestId = m_Coordinator->Submit(std::move(request));
		GGLAB_LOG_INFO_ALWAYS("Frame capture {} requested (source={}, timing={}{}{}).", requestId,
			GetFrameCaptureSourceName(source), GetFrameCaptureTimingName(timing),
			viewId.empty() ? "" : ", view=", viewId);
		return requestId;
	}

	bool ApplicationFrameCapture::Cancel(uint64_t requestId) noexcept
	{
		return m_Coordinator->Cancel(requestId);
	}

	const FrameCaptureRequestResult* ApplicationFrameCapture::FindResult(
		uint64_t requestId) const noexcept
	{
		for (const HistoryEntry& entry : m_History)
		{
			if (entry.m_Result.m_RequestId == requestId)
			{
				return &entry.m_Result;
			}
		}
		return nullptr;
	}

	std::vector<FrameCaptureRequestResult> ApplicationFrameCapture::Update() noexcept
	{
		std::vector<FrameCaptureRequestResult> results;
		m_Coordinator->ConsumeResults(results);
		for (const FrameCaptureRequestResult& result : results)
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
				.m_Result = result,
				.m_FinishedAt = std::chrono::steady_clock::now(),
				});
			if (m_History.size() > MaxHistoryEntries)
			{
				m_History.pop_front();
			}
		}
		return results;
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
