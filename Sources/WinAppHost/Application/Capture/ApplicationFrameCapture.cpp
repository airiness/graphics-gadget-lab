#include "Application/Capture/ApplicationFrameCapture.h"
#include "AppRuntimeLog.h"
#include "GGLabAppRuntime.h"

#include <algorithm>
#include <utility>

namespace gglab
{
	ApplicationFrameCapture::ApplicationFrameCapture(CreateInfo createInfo) noexcept :
		m_Coordinator(createInfo.m_Coordinator),
		m_TrySubmit(std::move(createInfo.m_TrySubmit)),
		m_Cancel(std::move(createInfo.m_Cancel)),
		m_TakeResult(std::move(createInfo.m_TakeResult)),
		m_OpenDirectory(std::move(createInfo.m_OpenDirectory)),
		m_DefaultOutputDirectory(std::move(createInfo.m_DefaultOutputDirectory))
	{
		GGLAB_ASSERT_NOT_NULL(m_Coordinator);
		GGLAB_ASSERT(m_TrySubmit && m_Cancel && m_TakeResult);
	}

	ApplicationFrameCapture::ApplicationFrameCapture(GGLabAppRuntime& runtime,
		std::filesystem::path defaultOutputDirectory, OpenDirectoryCallback openDirectory) noexcept :
		ApplicationFrameCapture(CreateInfo{
			.m_Coordinator = runtime.GetFrameCaptureCoordinator(),
			.m_DefaultOutputDirectory = std::move(defaultOutputDirectory),
			.m_TrySubmit = [&runtime](CapturePlan&& plan) { return runtime.TrySubmitCaptureJob(std::move(plan)); },
			.m_Cancel = [&runtime](uint64_t id) { return runtime.CancelCaptureJob(id); },
			.m_TakeResult = [&runtime](uint64_t id) { return runtime.TakeCaptureJobResult(id); },
			.m_OpenDirectory = std::move(openDirectory),
			})
	{
	}

	CapturePlan ApplicationFrameCapture::MakePlan() const noexcept
	{
		return {
			.m_Timing = m_Settings.m_Timing,
			.m_SettleFrames = m_Settings.m_Timing == FrameCaptureTiming::AfterReady ? m_Settings.m_SettleFrames : 0,
			.m_ReferenceViewId = m_Settings.m_ReferenceViewId,
			.m_OutputDirectory = m_Settings.m_OutputDirectory,
			.m_Label = m_Settings.m_Label,
			.m_Note = m_Settings.m_Note,
		};
	}

	CaptureAdmission ApplicationFrameCapture::Capture() noexcept
	{
		CapturePlan plan = MakePlan();
		plan.m_RequiredProducts.emplace_back(ImageCaptureProduct{ m_Settings.m_Source });
		return Submit(std::move(plan));
	}

	CaptureAdmission ApplicationFrameCapture::ExportGraph(bool summary) noexcept
	{
		CapturePlan plan = MakePlan();
		plan.m_RequiredProducts.emplace_back(GraphCaptureProduct{ summary });
		return Submit(std::move(plan));
	}

	CaptureAdmission ApplicationFrameCapture::ExportResources(std::vector<RGResourceAddress> targets,
		bool includeGraph) noexcept
	{
		CapturePlan plan = MakePlan();
		plan.m_RequiredProducts.emplace_back(ResourceCaptureProduct{ std::move(targets) });
		if (includeGraph)
		{
			plan.m_RequiredProducts.emplace_back(GraphCaptureProduct{});
		}
		return Submit(std::move(plan));
	}

	CaptureAdmission ApplicationFrameCapture::Submit(CapturePlan&& plan) noexcept
	{
		const CaptureAdmission previous = m_LastAdmission;
		m_LastAdmission = m_TrySubmit(std::move(plan));
		m_LastAdmissionTime = std::chrono::steady_clock::now();
		if (m_LastAdmission.m_Status == CaptureAdmissionStatus::Accepted)
		{
			m_PendingJobs.push_back(m_LastAdmission.m_JobId);
			GGLAB_LOG_INFO_ALWAYS("Interactive capture job {} accepted.", m_LastAdmission.m_JobId);
		}
		else if (previous.m_Status != m_LastAdmission.m_Status || previous.m_Reason != m_LastAdmission.m_Reason)
		{
			GGLAB_LOG_WARN_ALWAYS("Interactive capture admission {}: {}.",
				m_LastAdmission.m_Status == CaptureAdmissionStatus::Busy ? "busy" : "rejected",
				GetAdmissionReasonName(m_LastAdmission.m_Reason));
		}
		return m_LastAdmission;
	}

	CaptureCancelResult ApplicationFrameCapture::Cancel(uint64_t jobId) noexcept
	{
		if (FindResult(jobId))
		{
			return CaptureCancelResult::Finished;
		}
		return std::ranges::find(m_PendingJobs, jobId) == m_PendingJobs.end() ?
			CaptureCancelResult::NotFound : m_Cancel(jobId);
	}

	uint32_t ApplicationFrameCapture::CaptureReferenceViews() noexcept
	{
		const FrameCaptureFrameState* frameState = GetLastFrameState();
		if (!frameState || frameState->m_ReferenceViewIds.empty() ||
			m_Batch.m_State == ReferenceViewBatchState::Running || m_Batch.m_JobId != 0)
		{
			return 0;
		}
		m_BatchViews = frameState->m_ReferenceViewIds;
		m_BatchPlan = MakePlan();
		m_BatchPlan.m_RequiredProducts.emplace_back(ImageCaptureProduct{ m_Settings.m_Source });
		m_BatchPlan.m_WaitForCapacity = true;
		m_Batch = { ReferenceViewBatchState::Running, static_cast<uint32_t>(m_BatchViews.size()) };
		AdvanceReferenceViews();
		return m_Batch.m_TotalViews;
	}

	void ApplicationFrameCapture::AdvanceReferenceViews() noexcept
	{
		if (m_Batch.m_State != ReferenceViewBatchState::Running || m_Batch.m_JobId != 0)
		{
			return;
		}
		if (m_Batch.m_AcceptedViews == m_Batch.m_TotalViews)
		{
			m_Batch.m_State = ReferenceViewBatchState::Completed;
			m_BatchViews.clear();
			m_BatchPlan = {};
			return;
		}
		m_BatchPlan.m_ReferenceViewId = m_BatchViews[m_Batch.m_AcceptedViews];
		// Keep the template after Accepted moves its argument and after Busy.
		CapturePlan plan = m_BatchPlan;
		const CaptureAdmission admission = Submit(std::move(plan));
		if (admission.m_Status == CaptureAdmissionStatus::Accepted)
		{
			m_Batch.m_JobId = admission.m_JobId;
			++m_Batch.m_AcceptedViews;
		}
		else if (admission.m_Status == CaptureAdmissionStatus::Rejected)
		{
			m_Batch.m_State = ReferenceViewBatchState::Failed;
			m_BatchViews.clear();
			m_BatchPlan = {};
		}
	}

	CaptureCancelResult ApplicationFrameCapture::CancelReferenceViews() noexcept
	{
		if (m_Batch.m_State != ReferenceViewBatchState::Running)
		{
			return CaptureCancelResult::Finished;
		}
		m_Batch.m_State = ReferenceViewBatchState::Cancelled;
		m_BatchViews.clear();
		m_BatchPlan = {};
		return m_Batch.m_JobId == 0 ? CaptureCancelResult::Cancelled : Cancel(m_Batch.m_JobId);
	}

	const CaptureJobResult* ApplicationFrameCapture::FindResult(uint64_t jobId) const noexcept
	{
		for (const HistoryEntry& entry : m_History)
		{
			if (entry.m_Result.m_JobId == jobId)
			{
				return &entry.m_Result;
			}
		}
		return nullptr;
	}

	uint32_t ApplicationFrameCapture::Update(bool admitReferenceViews) noexcept
	{
		uint32_t finished = 0;
		for (auto pending = m_PendingJobs.begin(); pending != m_PendingJobs.end();)
		{
			auto result = m_TakeResult(*pending);
			if (!result)
			{
				++pending;
				continue;
			}
			if (result->m_JobId == m_Batch.m_JobId)
			{
				m_Batch.m_JobId = 0;
				if (IsPublished(*result))
				{
					++m_Batch.m_CompletedViews;
				}
				else if (m_Batch.m_State == ReferenceViewBatchState::Running)
				{
					m_Batch.m_State = ReferenceViewBatchState::Failed;
					m_BatchViews.clear();
					m_BatchPlan = {};
				}
			}
			GGLAB_LOG_INFO_ALWAYS("Interactive capture job {} finished (published={}): {}.",
				result->m_JobId, IsPublished(*result), GetPathText(result->m_ManifestPath));
			if (!IsPublished(*result))
			{
				GGLAB_LOG_WARN_ALWAYS("Interactive capture job {}: {}.", result->m_JobId, GetFailure(*result));
			}
			m_History.push_back({ std::move(*result), std::chrono::steady_clock::now() });
			if (m_History.size() > MaxHistoryEntries)
			{
				m_History.pop_front();
			}
			pending = m_PendingJobs.erase(pending);
			++finished;
		}
		if (admitReferenceViews)
		{
			AdvanceReferenceViews();
		}
		return finished;
	}

	bool ApplicationFrameCapture::IsPublished(const CaptureJobResult& result) noexcept
	{
		return result.m_Status == CaptureJobStatus::Completed &&
			result.m_Publication == CaptureJobPublication::Published;
	}

	const std::filesystem::path& ApplicationFrameCapture::GetOutputPath(const CaptureJobResult& result) noexcept
	{
		for (const auto& product : result.m_Products)
		{
			if (product.m_Kind == CaptureProductKind::Image && !product.m_Files.empty())
			{
				return product.m_Files.front();
			}
		}
		return result.m_ManifestPath;
	}

	const FrameCaptureMetadata* ApplicationFrameCapture::GetMetadata(const CaptureJobResult& result) noexcept
	{
		for (const auto& product : result.m_Products)
		{
			if (product.m_Metadata)
			{
				return &*product.m_Metadata;
			}
		}
		return nullptr;
	}

	std::string_view ApplicationFrameCapture::GetFailure(const CaptureJobResult& result) noexcept
	{
		for (const auto& product : result.m_Products)
		{
			if (!product.m_Failure.empty())
			{
				return product.m_Failure;
			}
		}
		return "No completed published bundle.";
	}

	std::string ApplicationFrameCapture::GetPathText(const std::filesystem::path& path) noexcept
	{
		return GetFrameCapturePathText(path);
	}

	std::string_view ApplicationFrameCapture::GetAdmissionReasonName(CaptureAdmissionReason reason) noexcept
	{
		switch (reason)
		{
		case CaptureAdmissionReason::None:
			return "None";
		case CaptureAdmissionReason::ActiveJobLimit:
			return "ActiveJobLimit";
		case CaptureAdmissionReason::ReceiptLimit:
			return "ReceiptLimit";
		case CaptureAdmissionReason::InvalidPlan:
			return "InvalidPlan";
		case CaptureAdmissionReason::PlanTooLarge:
			return "PlanTooLarge";
		case CaptureAdmissionReason::ShuttingDown:
			return "ShuttingDown";
		case CaptureAdmissionReason::IdentityExhausted:
			return "IdentityExhausted";
		case CaptureAdmissionReason::IdentityUnavailable:
			return "IdentityUnavailable";
		case CaptureAdmissionReason::ServicesUnavailable:
			return "ServicesUnavailable";
		}
		return "Unknown";
	}

	std::string_view ApplicationFrameCapture::GetPublicationName(CaptureJobPublication publication) noexcept
	{
		switch (publication)
		{
		case CaptureJobPublication::NotPublished:
			return "NotPublished";
		case CaptureJobPublication::Published:
			return "Published";
		case CaptureJobPublication::PartiallyPublished:
			return "PartiallyPublished";
		case CaptureJobPublication::Indeterminate:
			return "Indeterminate";
		}
		return "Unknown";
	}

	std::string_view ApplicationFrameCapture::GetCancellationName(CaptureCancelResult cancellation) noexcept
	{
		switch (cancellation)
		{
		case CaptureCancelResult::Cancelled:
			return "Cancelled";
		case CaptureCancelResult::TooLate:
			return "TooLate";
		case CaptureCancelResult::Finished:
			return "Finished";
		case CaptureCancelResult::NotFound:
			return "NotFound";
		}
		return "Unknown";
	}

	bool ApplicationFrameCapture::CanOpenOutputFolder(const CaptureJobResult& result) const noexcept
	{
		return m_OpenDirectory && IsPublished(result) && !result.m_ManifestPath.empty();
	}

	bool ApplicationFrameCapture::OpenOutputFolder(uint64_t jobId, std::string& failure) const noexcept
	{
		failure.clear();
		const CaptureJobResult* result = FindResult(jobId);
		if (!result || !IsPublished(*result) || result->m_ManifestPath.empty())
		{
			failure = "No completed published bundle is retained for this job.";
			return false;
		}
		if (!m_OpenDirectory)
		{
			failure = "Opening output folders is unavailable in this host.";
			return false;
		}
		const bool accepted = m_OpenDirectory(result->m_ManifestPath.parent_path(), failure);
		if (!accepted && failure.empty())
		{
			failure = "The host did not accept the request to open the output folder.";
		}
		return accepted;
	}

	uint32_t ApplicationFrameCapture::GetPendingJobCount() const noexcept
	{
		return static_cast<uint32_t>(m_PendingJobs.size());
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
