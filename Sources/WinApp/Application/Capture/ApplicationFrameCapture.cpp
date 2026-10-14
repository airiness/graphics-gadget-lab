#include "Application/Capture/ApplicationFrameCapture.h"
#include "AppRuntimeLog.h"
#include "GGLabAppRuntime.h"

#include <utility>
#include <vector>

namespace gglab
{
	ApplicationFrameCapture::ApplicationFrameCapture(CreateInfo createInfo) noexcept :
		m_Coordinator(createInfo.m_Coordinator),
		m_ConsumeResults(std::move(createInfo.m_ConsumeResults)),
		m_OpenDirectory(std::move(createInfo.m_OpenDirectory)),
		m_DefaultOutputDirectory(std::move(createInfo.m_DefaultOutputDirectory))
	{
		GGLAB_ASSERT_NOT_NULL(m_Coordinator);
		GGLAB_ASSERT(m_ConsumeResults);
	}

	ApplicationFrameCapture::ApplicationFrameCapture(GGLabAppRuntime& runtime,
		std::filesystem::path defaultOutputDirectory, OpenDirectoryCallback openDirectory) noexcept :
		ApplicationFrameCapture(CreateInfo{
			.m_Coordinator = runtime.GetFrameCaptureCoordinator(),
			.m_DefaultOutputDirectory = std::move(defaultOutputDirectory),
			.m_ConsumeResults = [&runtime](std::vector<FrameCaptureRequestResult>& results)
				{ runtime.ConsumeFrameCaptureResults(results); },
			.m_OpenDirectory = std::move(openDirectory),
			})
	{
	}

	FrameCaptureRequest ApplicationFrameCapture::MakeRequest(FrameCaptureSource source) const noexcept
	{
		const bool afterReady = m_Settings.m_Timing == FrameCaptureTiming::AfterReady;
		return {
			.m_Source = source,
			.m_Timing = m_Settings.m_Timing,
			.m_SettleFrames = afterReady ? m_Settings.m_SettleFrames : 0,
			.m_ReferenceViewId = m_Settings.m_ReferenceViewId,
			.m_OutputDirectory = m_Settings.m_OutputDirectory,
			.m_Label = m_Settings.m_Label,
			.m_Note = m_Settings.m_Note,
		};
	}

	uint64_t ApplicationFrameCapture::Capture() noexcept
	{
		return Submit(MakeRequest(m_Settings.m_Source));
	}

	uint64_t ApplicationFrameCapture::ExportGraph(bool summary) noexcept
	{
		FrameCaptureRequest request = MakeRequest(FrameCaptureSource::RenderGraph);
		request.m_GraphSummary = summary;
		return Submit(std::move(request));
	}

	uint64_t ApplicationFrameCapture::ExportResources(std::vector<RGResourceAddress> targets,
		bool includeGraph) noexcept
	{
		FrameCaptureRequest request = MakeRequest(FrameCaptureSource::ResourceExport);
		request.m_ResourceTargets = std::move(targets);
		request.m_IncludeGraph = includeGraph;
		return Submit(std::move(request));
	}

	uint32_t ApplicationFrameCapture::CaptureReferenceViews() noexcept
	{
		const FrameCaptureFrameState* frameState = m_Coordinator->GetLastFrameState();
		if (!frameState)
		{
			return 0;
		}
		const std::vector<std::string>& viewIds = frameState->m_ReferenceViewIds;
		for (const std::string& viewId : viewIds)
		{
			FrameCaptureRequest request = MakeRequest(m_Settings.m_Source);
			request.m_ReferenceViewId = viewId;
			request.m_WaitForCapacity = true;
			Submit(std::move(request));
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
		m_ConsumeResults(results);
		for (const FrameCaptureRequestResult& result : results)
		{
			if (result.m_Status == FrameCaptureRequestStatus::Completed)
			{
				GGLAB_LOG_INFO_ALWAYS("Frame capture {} written to '{}'.", result.m_RequestId,
					GetPathText(GetOutputPath(result)));
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

	const std::filesystem::path& ApplicationFrameCapture::GetOutputPath(
		const FrameCaptureRequestResult& result) noexcept
	{
		return result.m_ImagePath.empty() ? result.m_MetadataPath : result.m_ImagePath;
	}

	std::string ApplicationFrameCapture::GetPathText(const std::filesystem::path& path) noexcept
	{
		return GetFrameCapturePathText(path);
	}

	bool ApplicationFrameCapture::CanOpenOutputFolder(
		const FrameCaptureRequestResult& result) const noexcept
	{
		return m_OpenDirectory && result.m_Status == FrameCaptureRequestStatus::Completed &&
			!GetOutputPath(result).empty();
	}

	bool ApplicationFrameCapture::OpenOutputFolder(uint64_t requestId, std::string& failure) const noexcept
	{
		failure.clear();
		const FrameCaptureRequestResult* result = FindResult(requestId);
		if (!result || result->m_Status != FrameCaptureRequestStatus::Completed ||
			GetOutputPath(*result).empty())
		{
			failure = "No completed output is retained for this request.";
			return false;
		}
		if (!m_OpenDirectory)
		{
			failure = "Opening output folders is unavailable in this host.";
			return false;
		}
		const std::filesystem::path directory = GetOutputPath(*result).parent_path();
		const bool accepted = m_OpenDirectory(directory.empty() ? "." : directory, failure);
		if (!accepted && failure.empty())
		{
			failure = "The host did not accept the request to open the output folder.";
		}
		return accepted;
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
