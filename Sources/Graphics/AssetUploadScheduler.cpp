#include "Core/Precompiled.h"
#include "Graphics/AssetUploadScheduler.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/TransferBatch.h"
#include "Graphics/TransferManager.h"

#include <cmath>

namespace gglab
{
	namespace
	{
		constexpr uint32_t MaxResourcePublicationDrainSteps = 1'000'000;
		constexpr size_t RecentExecutionSampleCapacity = 256;

		template<typename Rep, typename Period>
		[[nodiscard]] double Milliseconds(std::chrono::duration<Rep, Period> duration) noexcept
		{
			return std::chrono::duration<double, std::milli>(duration).count();
		}

		[[nodiscard]] bool HasHigherPriority(
			TaskPriority lhs,
			TaskPriority rhs) noexcept
		{
			return static_cast<uint8_t>(lhs) < static_cast<uint8_t>(rhs);
		}

		void RecordExecutionSample(
			std::deque<double>& samples,
			double milliseconds) noexcept
		{
			samples.push_back(milliseconds);
			while (samples.size() > RecentExecutionSampleCapacity)
			{
				samples.pop_front();
			}
		}

		[[nodiscard]] double ExecutionP95(
			const std::deque<double>& samples)
		{
			if (samples.empty())
			{
				return 0.0;
			}
			std::vector<double> sorted(samples.begin(), samples.end());
			std::ranges::sort(sorted);
			const size_t index = std::min(
				static_cast<size_t>(std::ceil(static_cast<double>(sorted.size()) * 0.95)) - 1,
				sorted.size() - 1);
			return sorted[index];
		}
	}

	AssetUploadScheduler::AssetUploadScheduler(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_TransferManager(createInfo.m_TransferManager),
		m_OwnerThreadId(std::this_thread::get_id()),
		m_RecentUploadCapacity(createInfo.m_RecentUploadCapacity),
		m_FrameBudget(createInfo.m_FrameBudget)
	{
		GGLAB_ASSERT_NOT_NULL(m_Device);
		GGLAB_ASSERT_NOT_NULL(m_TransferManager);
	}

	AssetUploadScheduler::~AssetUploadScheduler()
	{
		GGLAB_ASSERT_MSG(
			m_CpuPayloadQueue.empty() &&
			m_ResourcePublicationQueue.empty() &&
			m_UploadRecordingQueue.empty() &&
			!m_RecordingBatch &&
			m_RecordedUploads.empty() &&
			m_PendingUploads.empty() &&
			m_GpuFinalizeQueue.empty(),
			"AssetUploadScheduler destroyed with pending streaming work. Call Finalize after draining ready work and waiting for GPU idle.");
	}

	void AssetUploadScheduler::EnqueueCpuPayload(
		AssetStreamingWorkDesc desc,
		AssetStreamingWork work) noexcept
	{
		EnqueueWork(
			m_CpuPayloadQueue,
			m_CpuPayloadTelemetry,
			std::move(desc),
			std::move(work));
	}

	void AssetUploadScheduler::EnqueueResourcePublication(
		AssetStreamingWorkDesc desc,
		std::unique_ptr<IResourcePublicationJob>&& job) noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "Asset resource publication must be enqueued on the scheduler owner thread.");
		if (!IsOwnerThread() || !job)
		{
			return;
		}

		QueuedResourcePublication publication{
			.m_Desc = std::move(desc),
			.m_QueuedAt = std::chrono::steady_clock::now(),
			.m_RemainingSourceBytes = 0,
			.m_HasStarted = false,
			.m_Job = std::move(job),
		};
		publication.m_RemainingSourceBytes = publication.m_Desc.m_Estimate.m_SourceBytes;
		m_ReadyPayloadBytes += publication.m_RemainingSourceBytes;
		m_ReadyPayloadHighWatermark = std::max(
			m_ReadyPayloadHighWatermark,
			m_ReadyPayloadBytes);
		InsertResourcePublication(std::move(publication));
		++m_ResourcePublicationTelemetry.m_EnqueuedCount;
		m_ResourcePublicationTelemetry.m_HighWatermark = std::max(
			m_ResourcePublicationTelemetry.m_HighWatermark,
			static_cast<uint32_t>(m_ResourcePublicationQueue.size()));
	}

	void AssetUploadScheduler::EnqueueUploadRecording(
		AssetStreamingWorkDesc desc,
		AssetStreamingWork work) noexcept
	{
		EnqueueWork(
			m_UploadRecordingQueue,
			m_UploadRecordingTelemetry,
			std::move(desc),
			std::move(work));
	}

	uint32_t AssetUploadScheduler::CancelReadyWork(
		const AssetContentVersion& contentVersion) noexcept
	{
		const AssetStreamingIdentity identity = ToAssetStreamingIdentity(contentVersion);
		if (!contentVersion.IsValid() || identity.m_Kind == AssetStreamingWorkKind::Unknown)
		{
			return 0;
		}
		return CancelReadyWork(identity);
	}

	uint32_t AssetUploadScheduler::CancelReadyWork(
		const AssetStreamingIdentity& identity) noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "Asset ready work cancellation must run on the owner thread.");
		if (!IsOwnerThread())
		{
			return 0;
		}

		return CancelQueuedWork(
			m_CpuPayloadQueue,
			m_CpuPayloadTelemetry,
			identity,
			false) +
			CancelResourcePublication(
				identity,
				AssetResourcePublicationAbortReason::Cancelled) +
			CancelQueuedWork(
				m_UploadRecordingQueue,
				m_UploadRecordingTelemetry,
				identity,
				true);
	}

	uint32_t AssetUploadScheduler::UpdateWorkPriority(
		const AssetContentVersion& contentVersion,
		TaskPriority priority) noexcept
	{
		const AssetStreamingIdentity identity = ToAssetStreamingIdentity(contentVersion);
		if (!contentVersion.IsValid() || identity.m_Kind == AssetStreamingWorkKind::Unknown)
		{
			return 0;
		}
		return UpdateWorkPriority(identity, priority);
	}

	uint32_t AssetUploadScheduler::UpdateWorkPriority(
		const AssetStreamingIdentity& identity,
		TaskPriority priority) noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "Asset streaming work reprioritization must run on the owner thread.");
		if (!IsOwnerThread() || priority == TaskPriority::Count)
		{
			return 0;
		}

		uint32_t updatedCount =
			UpdateQueuedWorkPriority(m_CpuPayloadQueue, identity, priority) +
			UpdateResourcePublicationPriority(identity, priority) +
			UpdateQueuedWorkPriority(m_UploadRecordingQueue, identity, priority);
		for (PendingUpload& upload : m_PendingUploads)
		{
			if (upload.m_Desc.m_Identity == identity && upload.m_Desc.m_Priority != priority)
			{
				upload.m_Desc.m_Priority = priority;
				++updatedCount;
			}
		}
		for (PendingGpuFinalize& publication : m_GpuFinalizeQueue)
		{
			if (publication.m_Upload.m_Desc.m_Identity == identity &&
				publication.m_Upload.m_Desc.m_Priority != priority)
			{
				publication.m_Upload.m_Desc.m_Priority = priority;
				++updatedCount;
			}
		}
		if (updatedCount > 0)
		{
			std::ranges::stable_sort(m_GpuFinalizeQueue,
				[](const PendingGpuFinalize& lhs, const PendingGpuFinalize& rhs) noexcept
				{
					return HasHigherPriority(
						lhs.m_Upload.m_Desc.m_Priority,
						rhs.m_Upload.m_Desc.m_Priority);
				});
		}
		return updatedCount;
	}

	AssetUploadHandle AssetUploadScheduler::RecordUpload(
		AssetUploadDesc desc,
		AssetUploadRecord record,
		AssetUploadCompletion completion) noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "Asset uploads must be recorded on the scheduler owner thread.");
		GGLAB_ASSERT_MSG(
			m_IsRecordingUploadBatch,
			"Asset uploads must be recorded while the upload-recording queue is being drained.");
		if (!IsOwnerThread() || !m_IsRecordingUploadBatch || !record)
		{
			return {};
		}
		if (!m_RecordingBatch)
		{
			m_RecordingBatch = std::make_unique<TransferBatch>(
				m_TransferManager->BeginBatch());
		}

		PendingUpload upload{};
		upload.m_Handle = { m_NextHandle++ };
		upload.m_Desc = std::move(desc);
		upload.m_Completion = std::move(completion);
		try
		{
			upload.m_RecordingSucceeded = record(*m_RecordingBatch);
		}
		catch (const std::exception& exception)
		{
			GGLAB_UNUSED(exception);
			GGLAB_LOG_GRAPHICS_ERROR(
				"Asset upload recording '{}' threw an exception: {}",
				upload.m_Desc.m_Name,
				exception.what());
		}
		catch (...)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Asset upload recording '{}' threw an unknown exception.",
				upload.m_Desc.m_Name);
		}

		const AssetUploadHandle handle = upload.m_Handle;
		m_RecordedUploadBytes += upload.m_Desc.m_Estimate.m_StagingBytes;
		m_RecordedUploads.emplace_back(std::move(upload));
		return handle;
	}

	void AssetUploadScheduler::FlushRecordedUploads() noexcept
	{
		if (m_RecordedUploads.empty())
		{
			GGLAB_ASSERT_MSG(
				!m_RecordingBatch && m_RecordedUploadBytes == 0,
				"An empty upload batch should not own transfer state.");
			return;
		}

		GGLAB_ASSERT_NOT_NULL(m_RecordingBatch.get());
		const uint32_t uploadCount = static_cast<uint32_t>(m_RecordedUploads.size());
		const auto submittedAt = std::chrono::steady_clock::now();
		const RHIFencePoint fencePoint = m_RecordingBatch->Submit(false);
		m_RecordingBatch.reset();
		++m_BatchSubmissionCount;
		m_LastBatchUploadCount = uploadCount;
		m_MaxUploadsPerBatch = std::max(m_MaxUploadsPerBatch, uploadCount);
		m_SubmittedCount += uploadCount;

		for (PendingUpload& recorded : m_RecordedUploads)
		{
			recorded.m_SubmittedAt = submittedAt;
			recorded.m_FencePoint = fencePoint;
			if (!fencePoint.IsValid())
			{
				ProgressReporter(recorded.m_Desc.m_Progress).Report(
					0.72f,
					"GPU upload submission failed",
					recorded.m_Desc.m_Name);
				EnqueueGpuFinalize(std::move(recorded), AssetUploadStatus::Failed);
				continue;
			}

			ProgressReporter(recorded.m_Desc.m_Progress).Report(
				0.82f,
				"Waiting for GPU upload",
				std::format("{} | fence {}", recorded.m_Desc.m_Name, fencePoint.m_Value));
			m_InFlightBytes += recorded.m_Desc.m_Estimate.m_StagingBytes;
			m_PendingUploads.emplace_back(std::move(recorded));
		}
		m_RecordedUploads.clear();
		m_RecordedUploadBytes = 0;
		m_InFlightHighWatermark = std::max(m_InFlightHighWatermark, m_InFlightBytes);
	}

	uint32_t AssetUploadScheduler::Tick() noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "Asset upload fences must be polled on the scheduler owner thread.");
		if (!IsOwnerThread())
		{
			return 0;
		}

		GGLAB_UNUSED(PollCompletedUploads());
		m_LastFrameUsage = {};
		GGLAB_UNUSED(DrainCpuPayloadQueue(false));
		GGLAB_UNUSED(DrainResourcePublicationQueue(false));
		GGLAB_UNUSED(DrainUploadRecordingQueue(false));
		GGLAB_UNUSED(PollCompletedUploads());
		return DrainGpuFinalizeQueue(false);
	}

	void AssetUploadScheduler::DrainReadyWork() noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "AssetUploadScheduler::DrainReadyWork must run on the owner thread.");
		if (!IsOwnerThread())
		{
			return;
		}

		m_LastFrameUsage = {};
		while (!m_CpuPayloadQueue.empty() ||
			!m_ResourcePublicationQueue.empty() ||
			!m_UploadRecordingQueue.empty())
		{
			GGLAB_UNUSED(DrainCpuPayloadQueue(true));
			GGLAB_UNUSED(DrainResourcePublicationQueue(true));
			GGLAB_UNUSED(DrainUploadRecordingQueue(true));
		}
		GGLAB_UNUSED(PollCompletedUploads());
		GGLAB_UNUSED(DrainGpuFinalizeQueue(true));
	}

	uint32_t AssetUploadScheduler::PollCompletedUploads() noexcept
	{
		std::vector<PendingUpload> completed;
		for (auto iterator = m_PendingUploads.begin(); iterator != m_PendingUploads.end();)
		{
			if (iterator->m_Desc.m_Identity == m_GpuCompletionHold)
			{
				++iterator;
				continue;
			}
			if (!m_Device->IsFencePointCompleted(iterator->m_FencePoint))
			{
				++iterator;
				continue;
			}

			completed.emplace_back(std::move(*iterator));
			iterator = m_PendingUploads.erase(iterator);
		}

		if (!completed.empty())
		{
			m_TransferManager->Reclaim();
		}
		for (PendingUpload& upload : completed)
		{
			const uint64_t stagingBytes = upload.m_Desc.m_Estimate.m_StagingBytes;
			GGLAB_ASSERT_MSG(
				stagingBytes <= m_InFlightBytes,
				"Asset upload in-flight byte accounting underflow.");
			m_InFlightBytes = stagingBytes <= m_InFlightBytes ?
				m_InFlightBytes - stagingBytes : 0;
			const AssetUploadStatus status = upload.m_RecordingSucceeded ?
				AssetUploadStatus::Succeeded : AssetUploadStatus::Failed;
			EnqueueGpuFinalize(std::move(upload), status);
		}
		return static_cast<uint32_t>(completed.size());
	}

	void AssetUploadScheduler::Finalize() noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "AssetUploadScheduler::Finalize must run on the owner thread.");
		if (!IsOwnerThread())
		{
			return;
		}

		m_GpuCompletionHold = {};
		GGLAB_ASSERT_MSG(
			m_CpuPayloadQueue.empty() &&
			m_ResourcePublicationQueue.empty() &&
			m_UploadRecordingQueue.empty(),
			"AssetUploadScheduler::Finalize requires ready work to be drained before the RHI context becomes idle.");
		GGLAB_UNUSED(PollCompletedUploads());
		GGLAB_UNUSED(DrainGpuFinalizeQueue(true));
		GGLAB_ASSERT_MSG(
			m_PendingUploads.empty(),
			"AssetUploadScheduler::Finalize requires the RHI context to be idle.");
		while (!m_PendingUploads.empty())
		{
			PendingUpload upload = std::move(m_PendingUploads.front());
			m_PendingUploads.pop_front();
			EnqueueGpuFinalize(std::move(upload), AssetUploadStatus::Failed);
		}
		GGLAB_UNUSED(DrainGpuFinalizeQueue(true));
		m_TransferManager->Reclaim();
	}

	void AssetUploadScheduler::SetFrameBudget(
		const AssetStreamingFrameBudget& budget) noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "Asset streaming frame budget must be changed on the owner thread.");
		if (IsOwnerThread())
		{
			m_FrameBudget = budget;
		}
	}

	void AssetUploadScheduler::ArmResourcePublicationFault(
		const AssetResourcePublicationFaultInjection& fault) noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "Resource publication fault injection must be configured on the owner thread.");
		if (!IsOwnerThread())
		{
			return;
		}
		GGLAB_ASSERT_MSG(fault.IsValid(), "Invalid resource publication fault injection configuration.");
		m_PublicationFault = fault.IsValid() ? fault : AssetResourcePublicationFaultInjection{};
		m_PublicationFaultObservedOccurrences = 0;
	}

	void AssetUploadScheduler::ClearResourcePublicationFault() noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "Resource publication fault injection must be cleared on the owner thread.");
		if (IsOwnerThread())
		{
			m_PublicationFault = {};
			m_PublicationFaultObservedOccurrences = 0;
		}
	}

	void AssetUploadScheduler::ArmGpuCompletionHold(
		const AssetStreamingIdentity& identity) noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "GPU completion hold must be configured on the owner thread.");
		GGLAB_ASSERT_MSG(
			identity.m_Kind != AssetStreamingWorkKind::Unknown,
			"GPU completion hold requires a valid streaming identity.");
		if (IsOwnerThread() && identity.m_Kind != AssetStreamingWorkKind::Unknown)
		{
			m_GpuCompletionHold = identity;
		}
	}

	void AssetUploadScheduler::ClearGpuCompletionHold() noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "GPU completion hold must be cleared on the owner thread.");
		if (IsOwnerThread())
		{
			m_GpuCompletionHold = {};
		}
	}

	AssetUploadStatistics AssetUploadScheduler::GetStatistics() const
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "AssetUploadScheduler statistics must be read on the owner thread.");
		AssetUploadStatistics statistics{};
		statistics.m_CpuPayloadQueue = BuildQueueStatistics(
			m_CpuPayloadQueue,
			m_CpuPayloadTelemetry);
		statistics.m_ResourcePublicationQueue =
			BuildResourcePublicationQueueStatistics();
		statistics.m_UploadRecordingQueue = BuildQueueStatistics(
			m_UploadRecordingQueue,
			m_UploadRecordingTelemetry);
		statistics.m_GpuFinalizeQueue = BuildGpuFinalizeQueueStatistics();
		statistics.m_FrameBudget = m_FrameBudget;
		statistics.m_LastFrameUsage = m_LastFrameUsage;
		statistics.m_ReadyPayloadBytes = m_ReadyPayloadBytes;
		statistics.m_ReadyPayloadHighWatermark = m_ReadyPayloadHighWatermark;
		statistics.m_InFlightBytes = m_InFlightBytes;
		statistics.m_InFlightHighWatermark = m_InFlightHighWatermark;
		statistics.m_UploadPromotionBudgetDeferralCount =
			m_UploadPromotionBudgetDeferralCount;
		statistics.m_UploadBudgetDeferralCount = m_UploadBudgetDeferralCount;
		statistics.m_InFlightBudgetDeferralCount = m_InFlightBudgetDeferralCount;
		statistics.m_OversizedAdmissionCount = m_OversizedAdmissionCount;
		statistics.m_PendingCount = static_cast<uint32_t>(m_PendingUploads.size());
		statistics.m_BatchSubmissionCount = m_BatchSubmissionCount;
		statistics.m_LastBatchUploadCount = m_LastBatchUploadCount;
		statistics.m_MaxUploadsPerBatch = m_MaxUploadsPerBatch;
		statistics.m_SubmittedCount = m_SubmittedCount;
		statistics.m_SucceededCount = m_SucceededCount;
		statistics.m_FailedCount = m_FailedCount;
		statistics.m_CompletionCallbackFailureCount = m_CompletionCallbackFailureCount;
		statistics.m_RecentUploads.assign(m_RecentUploads.begin(), m_RecentUploads.end());

		const auto now = std::chrono::steady_clock::now();
		statistics.m_PendingUploads.reserve(m_PendingUploads.size());
		for (const PendingUpload& upload : m_PendingUploads)
		{
			const ProgressSnapshot progress = upload.m_Desc.m_Progress ?
				upload.m_Desc.m_Progress->GetSnapshot() : ProgressSnapshot{};
			statistics.m_PendingUploads.push_back({
				.m_Handle = upload.m_Handle,
				.m_Name = upload.m_Desc.m_Name,
				.m_Identity = upload.m_Desc.m_Identity,
				.m_Estimate = upload.m_Desc.m_Estimate,
				.m_Status = AssetUploadStatus::Pending,
				.m_FencePoint = upload.m_FencePoint,
				.m_ElapsedMilliseconds = Milliseconds(now - upload.m_SubmittedAt),
				.m_Progress = progress,
			});
		}
		return statistics;
	}

	bool AssetUploadScheduler::IsOwnerThread() const noexcept
	{
		return std::this_thread::get_id() == m_OwnerThreadId;
	}

	void AssetUploadScheduler::EnqueueWork(
		std::deque<QueuedWork>& queue,
		QueueTelemetry& telemetry,
		AssetStreamingWorkDesc desc,
		AssetStreamingWork work) noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "Asset streaming work must be enqueued on the scheduler owner thread.");
		if (!IsOwnerThread() || !work)
		{
			return;
		}

		QueuedWork queued{
			.m_Desc = std::move(desc),
			.m_QueuedAt = std::chrono::steady_clock::now(),
			.m_Work = std::move(work),
		};
		const uint64_t sourceBytes = queued.m_Desc.m_Estimate.m_SourceBytes;
		const auto insertion = std::ranges::find_if(queue,
			[priority = queued.m_Desc.m_Priority](const QueuedWork& existing) noexcept
			{
				return HasHigherPriority(priority, existing.m_Desc.m_Priority);
			});
		queue.insert(insertion, std::move(queued));
		++telemetry.m_EnqueuedCount;
		telemetry.m_HighWatermark = std::max(
			telemetry.m_HighWatermark,
			static_cast<uint32_t>(queue.size()));
		m_ReadyPayloadBytes += sourceBytes;
		if (&queue == &m_UploadRecordingQueue)
		{
			m_UploadRecordingBacklogBytes += sourceBytes;
		}
		m_ReadyPayloadHighWatermark = std::max(
			m_ReadyPayloadHighWatermark,
			m_ReadyPayloadBytes);
	}

	double AssetUploadScheduler::ExecuteWork(
		QueuedWork&& queued,
		QueueTelemetry& telemetry,
		std::string_view queueName) noexcept
	{
		const double queueMilliseconds = Milliseconds(
			std::chrono::steady_clock::now() - queued.m_QueuedAt);
		telemetry.m_TotalQueueMilliseconds += queueMilliseconds;
		++telemetry.m_QueueSampleCount;
		telemetry.m_MaxQueueMilliseconds = std::max(
			telemetry.m_MaxQueueMilliseconds,
			queueMilliseconds);

		const auto executionBegin = std::chrono::steady_clock::now();
		try
		{
			queued.m_Work();
		}
		catch (const std::exception& exception)
		{
			GGLAB_UNUSED(exception);
			++telemetry.m_CallbackFailureCount;
			GGLAB_LOG_GRAPHICS_ERROR(
				"Asset {} work '{}' threw an exception: {}",
				queueName,
				queued.m_Desc.m_Name,
				exception.what());
		}
		catch (...)
		{
			++telemetry.m_CallbackFailureCount;
			GGLAB_LOG_GRAPHICS_ERROR(
				"Asset {} work '{}' threw an unknown exception.",
				queueName,
				queued.m_Desc.m_Name);
		}
		const double executionMilliseconds = Milliseconds(
			std::chrono::steady_clock::now() - executionBegin);
		telemetry.m_TotalExecutionMilliseconds += executionMilliseconds;
		telemetry.m_MaxExecutionMilliseconds = std::max(
			telemetry.m_MaxExecutionMilliseconds,
			executionMilliseconds);
		RecordExecutionSample(
			telemetry.m_RecentExecutionMilliseconds,
			executionMilliseconds);
		++telemetry.m_ProcessedCount;
		return executionMilliseconds;
	}

	void AssetUploadScheduler::InsertResourcePublication(
		QueuedResourcePublication&& publication) noexcept
	{
		const auto insertion = std::ranges::find_if(m_ResourcePublicationQueue,
			[priority = publication.m_Desc.m_Priority](
				const QueuedResourcePublication& existing) noexcept
			{
				return HasHigherPriority(priority, existing.m_Desc.m_Priority);
			});
		m_ResourcePublicationQueue.insert(insertion, std::move(publication));
	}

	uint32_t AssetUploadScheduler::DrainCpuPayloadQueue(bool ignoreBudget) noexcept
	{
		const uint32_t initialCount = static_cast<uint32_t>(m_CpuPayloadQueue.size());
		uint32_t processedCount = 0;
		while (processedCount < initialCount && !m_CpuPayloadQueue.empty())
		{
			const QueuedWork& next = m_CpuPayloadQueue.front();
			if (!ignoreBudget && processedCount > 0 &&
				(m_LastFrameUsage.m_CpuPayloadItems >= m_FrameBudget.m_MaxCpuPayloadItems ||
				m_LastFrameUsage.m_CpuPayloadMilliseconds >= m_FrameBudget.m_MaxCpuPayloadMilliseconds))
			{
				break;
			}
			const uint64_t prospectiveUploadBacklog =
				m_UploadRecordingBacklogBytes + next.m_Desc.m_Estimate.m_SourceBytes;
			if (!ignoreBudget && !m_UploadRecordingQueue.empty() &&
				prospectiveUploadBacklog > m_FrameBudget.m_MaxUploadRecordingBacklogBytes)
			{
				++m_UploadPromotionBudgetDeferralCount;
				break;
			}

			QueuedWork queued = std::move(m_CpuPayloadQueue.front());
			m_CpuPayloadQueue.pop_front();
			RemoveReadyPayload(queued.m_Desc.m_Estimate, false);
			const double elapsed = ExecuteWork(
				std::move(queued), m_CpuPayloadTelemetry, "CPU-payload");
			++processedCount;
			++m_LastFrameUsage.m_CpuPayloadItems;
			m_LastFrameUsage.m_CpuPayloadMilliseconds += elapsed;
		}
		return processedCount;
	}

	bool AssetUploadScheduler::TryApplyResourcePublicationFault(
		const AssetStreamingIdentity& identity,
		AssetResourcePublicationStage stage,
		AssetResourcePublicationFaultTiming timing,
		AssetResourcePublicationStepResult& result) noexcept
	{
		if (!m_PublicationFault.IsValid() ||
			m_PublicationFault.m_Identity != identity ||
			m_PublicationFault.m_Stage != stage ||
			m_PublicationFault.m_Timing != timing)
		{
			return false;
		}

		++m_PublicationFaultObservedOccurrences;
		if (m_PublicationFaultObservedOccurrences <
			m_PublicationFault.m_TriggerOccurrence)
		{
			return false;
		}

		const AssetResourcePublicationFaultAction action =
			m_PublicationFault.m_Action;
		result.m_Status = action == AssetResourcePublicationFaultAction::Fail ?
			AssetResourcePublicationStepStatus::Failed :
			AssetResourcePublicationStepStatus::Cancelled;
		result.m_Usage.m_Stage = stage;
		result.m_Error = std::format(
			"Injected publication {} {} stage {} occurrence {}",
			action == AssetResourcePublicationFaultAction::Fail ? "failure" : "cancellation",
			timing == AssetResourcePublicationFaultTiming::BeforeStep ? "before" : "after",
			static_cast<size_t>(stage),
			m_PublicationFaultObservedOccurrences);
		++m_PublicationFaultInjectionCount;
		m_PublicationFault = {};
		m_PublicationFaultObservedOccurrences = 0;
		return true;
	}

	uint32_t AssetUploadScheduler::DrainResourcePublicationQueue(bool ignoreBudget) noexcept
	{
		uint32_t processedCount = 0;
		AssetResourcePublicationContext context{
			.m_Scheduler = this,
		};
		while (!m_ResourcePublicationQueue.empty())
		{
			const bool budgetExhausted =
				m_LastFrameUsage.m_ResourcePublicationSteps >=
					m_FrameBudget.m_MaxResourcePublicationSteps ||
				m_LastFrameUsage.m_ResourcePublicationCreations >=
					m_FrameBudget.m_MaxResourcePublicationCreations ||
				m_LastFrameUsage.m_ResourcePublicationMilliseconds >=
					m_FrameBudget.m_MaxResourcePublicationMilliseconds;
			if (!ignoreBudget && processedCount > 0 && budgetExhausted)
			{
				break;
			}
			if (ignoreBudget && processedCount >= MaxResourcePublicationDrainSteps)
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"Resource publication drain exceeded {} steps; aborting {} remaining jobs.",
					MaxResourcePublicationDrainSteps,
					m_ResourcePublicationQueue.size());
				while (!m_ResourcePublicationQueue.empty())
				{
					QueuedResourcePublication publication =
						std::move(m_ResourcePublicationQueue.front());
					m_ResourcePublicationQueue.pop_front();
					publication.m_Job->Abort(
						context,
						AssetResourcePublicationAbortReason::Shutdown);
					GGLAB_ASSERT_MSG(
						publication.m_RemainingSourceBytes <= m_ReadyPayloadBytes,
						"Resource publication payload accounting underflow during drain abort.");
					m_ReadyPayloadBytes = publication.m_RemainingSourceBytes <= m_ReadyPayloadBytes ?
						m_ReadyPayloadBytes - publication.m_RemainingSourceBytes : 0;
					++m_ResourcePublicationTelemetry.m_CancelledCount;
				}
				break;
			}

			QueuedResourcePublication publication =
				std::move(m_ResourcePublicationQueue.front());
			m_ResourcePublicationQueue.pop_front();
			context.m_Priority = publication.m_Desc.m_Priority;
			if (!publication.m_HasStarted)
			{
				const double queueMilliseconds = Milliseconds(
					std::chrono::steady_clock::now() - publication.m_QueuedAt);
				m_ResourcePublicationTelemetry.m_TotalQueueMilliseconds += queueMilliseconds;
				++m_ResourcePublicationTelemetry.m_QueueSampleCount;
				m_ResourcePublicationTelemetry.m_MaxQueueMilliseconds = std::max(
					m_ResourcePublicationTelemetry.m_MaxQueueMilliseconds,
					queueMilliseconds);
				publication.m_HasStarted = true;
			}

			const uint64_t progressBefore = publication.m_Job->GetProgressToken();
			const auto executionBegin = std::chrono::steady_clock::now();
			AssetResourcePublicationStepResult result{};
			const bool injectedBeforeStep = TryApplyResourcePublicationFault(
				publication.m_Desc.m_Identity,
				publication.m_Job->GetCurrentStage(),
				AssetResourcePublicationFaultTiming::BeforeStep,
				result);
			if (!injectedBeforeStep)
			{
				result = publication.m_Job->Step(context);
			}
			const double executionMilliseconds = Milliseconds(
				std::chrono::steady_clock::now() - executionBegin);
			const uint64_t progressAfter = publication.m_Job->GetProgressToken();
			m_ResourcePublicationTelemetry.m_TotalExecutionMilliseconds += executionMilliseconds;
			m_ResourcePublicationTelemetry.m_MaxExecutionMilliseconds = std::max(
				m_ResourcePublicationTelemetry.m_MaxExecutionMilliseconds,
				executionMilliseconds);
			RecordExecutionSample(
				m_ResourcePublicationTelemetry.m_RecentExecutionMilliseconds,
				executionMilliseconds);
			if (executionMilliseconds > m_FrameBudget.m_MaxResourcePublicationMilliseconds)
			{
				++m_PublicationOverBudgetExecutionCount;
			}
			++m_ResourcePublicationTelemetry.m_ProcessedCount;
			GGLAB_ASSERT_MSG(
				result.m_Usage.m_WorkItems == 1,
				"Each resource publication Step must report exactly one work item.");
			if (result.m_Status == AssetResourcePublicationStepStatus::Continue &&
				progressAfter == progressBefore)
			{
				++m_PublicationNoProgressContinueCount;
				GGLAB_ASSERT_MSG(false,
					"Resource publication returned Continue without advancing its progress token.");
			}
			const size_t stageIndex = static_cast<size_t>(result.m_Usage.m_Stage);
			if (stageIndex < m_PublicationStageTelemetry.size())
			{
				PublicationStageTelemetry& stageTelemetry =
					m_PublicationStageTelemetry[stageIndex];
				++stageTelemetry.m_StepCount;
				stageTelemetry.m_TotalMilliseconds += executionMilliseconds;
				stageTelemetry.m_MaxMilliseconds = std::max(
					stageTelemetry.m_MaxMilliseconds,
					executionMilliseconds);
				RecordExecutionSample(
					stageTelemetry.m_RecentExecutionMilliseconds,
					executionMilliseconds);
			}
			if (result.m_Status == AssetResourcePublicationStepStatus::Continue)
			{
				GGLAB_UNUSED(TryApplyResourcePublicationFault(
					publication.m_Desc.m_Identity,
					result.m_Usage.m_Stage,
					AssetResourcePublicationFaultTiming::AfterStep,
					result));
			}
			m_ResourcePublicationTelemetry.m_ResourceCreationCount +=
				result.m_Usage.m_ResourceCreations;
			m_ResourcePublicationTelemetry.m_PayloadBytesMovedToUpload +=
				result.m_Usage.m_PayloadBytesMovedToUpload;
			m_ResourcePublicationTelemetry.m_PayloadBytesDestroyed +=
				result.m_Usage.m_PayloadBytesDestroyed;
			RetireResourcePublicationPayload(publication, result.m_Usage);

			++processedCount;
			++m_LastFrameUsage.m_ResourcePublicationSteps;
			m_LastFrameUsage.m_ResourcePublicationCreations = static_cast<uint32_t>(
				std::min<uint64_t>(
					static_cast<uint64_t>(m_LastFrameUsage.m_ResourcePublicationCreations) +
						result.m_Usage.m_ResourceCreations,
					std::numeric_limits<uint32_t>::max()));
			m_LastFrameUsage.m_ResourcePublicationMilliseconds += executionMilliseconds;

			switch (result.m_Status)
			{
			case AssetResourcePublicationStepStatus::Continue:
				++m_ResourcePublicationTelemetry.m_ContinueCount;
				InsertResourcePublication(std::move(publication));
				break;
			case AssetResourcePublicationStepStatus::Completed:
				++m_ResourcePublicationTelemetry.m_CompletedCount;
				break;
			case AssetResourcePublicationStepStatus::Failed:
				++m_ResourcePublicationTelemetry.m_FailedCount;
				publication.m_Job->Abort(
					context,
					AssetResourcePublicationAbortReason::Failed);
				GGLAB_LOG_GRAPHICS_ERROR(
					"Resource publication '{}' failed: {}",
					publication.m_Desc.m_Name,
					result.m_Error.empty() ? "unspecified error" : result.m_Error);
				break;
			case AssetResourcePublicationStepStatus::Cancelled:
				++m_ResourcePublicationTelemetry.m_CancelledCount;
				publication.m_Job->Abort(
					context,
					AssetResourcePublicationAbortReason::Cancelled);
				break;
			}

			if (result.m_Status != AssetResourcePublicationStepStatus::Continue)
			{
				GGLAB_ASSERT_MSG(
					publication.m_RemainingSourceBytes <= m_ReadyPayloadBytes,
					"Resource publication payload accounting underflow at terminal state.");
				m_ReadyPayloadBytes = publication.m_RemainingSourceBytes <= m_ReadyPayloadBytes ?
					m_ReadyPayloadBytes - publication.m_RemainingSourceBytes : 0;
			}
		}
		return processedCount;
	}

	uint32_t AssetUploadScheduler::DrainUploadRecordingQueue(bool ignoreBudget) noexcept
	{
		GGLAB_ASSERT_MSG(
			!m_IsRecordingUploadBatch && !m_RecordingBatch &&
			m_RecordedUploads.empty() && m_RecordedUploadBytes == 0,
			"Upload batch recording cannot be nested.");
		m_IsRecordingUploadBatch = true;
		const uint32_t initialCount = static_cast<uint32_t>(m_UploadRecordingQueue.size());
		uint32_t processedCount = 0;
		while (processedCount < initialCount && !m_UploadRecordingQueue.empty())
		{
			const AssetStreamingWorkEstimate& estimate =
				m_UploadRecordingQueue.front().m_Desc.m_Estimate;
			const bool hasFrameAdmission = m_LastFrameUsage.m_UploadRecordingItems > 0;
			const bool exceedsFrameBudget =
				m_LastFrameUsage.m_UploadRecordingItems >= m_FrameBudget.m_MaxUploadRecordingItems ||
				m_LastFrameUsage.m_UploadBytes + estimate.m_StagingBytes > m_FrameBudget.m_MaxUploadBytes ||
				m_LastFrameUsage.m_UploadOperations + estimate.m_OperationCount > m_FrameBudget.m_MaxUploadOperations ||
				m_LastFrameUsage.m_UploadRecordingMilliseconds >= m_FrameBudget.m_MaxUploadRecordingMilliseconds;
			if (!ignoreBudget && hasFrameAdmission && exceedsFrameBudget)
			{
				++m_UploadBudgetDeferralCount;
				break;
			}

			const bool exceedsInFlightBudget =
				m_InFlightBytes + m_RecordedUploadBytes + estimate.m_StagingBytes >
					m_FrameBudget.m_MaxInFlightBytes;
			if (!ignoreBudget && exceedsInFlightBudget && m_InFlightBytes > 0)
			{
				++m_InFlightBudgetDeferralCount;
				break;
			}
			if (!ignoreBudget && !hasFrameAdmission && (exceedsFrameBudget || exceedsInFlightBudget))
			{
				++m_OversizedAdmissionCount;
			}

			QueuedWork queued = std::move(m_UploadRecordingQueue.front());
			m_UploadRecordingQueue.pop_front();
			RemoveReadyPayload(queued.m_Desc.m_Estimate, true);
			const AssetStreamingWorkEstimate admittedEstimate = queued.m_Desc.m_Estimate;
			const double elapsed = ExecuteWork(
				std::move(queued), m_UploadRecordingTelemetry, "upload-recording");
			++processedCount;
			++m_LastFrameUsage.m_UploadRecordingItems;
			m_LastFrameUsage.m_UploadBytes += admittedEstimate.m_StagingBytes;
			m_LastFrameUsage.m_UploadOperations += admittedEstimate.m_OperationCount;
			m_LastFrameUsage.m_UploadRecordingMilliseconds += elapsed;
		}
		FlushRecordedUploads();
		m_IsRecordingUploadBatch = false;
		return processedCount;
	}

	void AssetUploadScheduler::EnqueueGpuFinalize(
		PendingUpload&& upload,
		AssetUploadStatus status) noexcept
	{
		PendingGpuFinalize publication{
			.m_Upload = std::move(upload),
			.m_Status = status,
			.m_QueuedAt = std::chrono::steady_clock::now(),
		};
		const auto insertion = std::ranges::find_if(m_GpuFinalizeQueue,
			[priority = publication.m_Upload.m_Desc.m_Priority](
				const PendingGpuFinalize& existing) noexcept
			{
				return HasHigherPriority(priority, existing.m_Upload.m_Desc.m_Priority);
			});
		m_GpuFinalizeQueue.insert(insertion, std::move(publication));
		++m_GpuFinalizeTelemetry.m_EnqueuedCount;
		m_GpuFinalizeTelemetry.m_HighWatermark = std::max(
			m_GpuFinalizeTelemetry.m_HighWatermark,
			static_cast<uint32_t>(m_GpuFinalizeQueue.size()));
	}

	uint32_t AssetUploadScheduler::DrainGpuFinalizeQueue(bool ignoreBudget) noexcept
	{
		const uint32_t initialCount = static_cast<uint32_t>(m_GpuFinalizeQueue.size());
		uint32_t processedCount = 0;
		while (processedCount < initialCount && !m_GpuFinalizeQueue.empty())
		{
			if (!ignoreBudget && processedCount > 0 &&
				(m_LastFrameUsage.m_GpuFinalizeItems >= m_FrameBudget.m_MaxGpuFinalizeItems ||
				m_LastFrameUsage.m_GpuFinalizeMilliseconds >= m_FrameBudget.m_MaxGpuFinalizeMilliseconds))
			{
				break;
			}
			PendingGpuFinalize publication = std::move(m_GpuFinalizeQueue.front());
			m_GpuFinalizeQueue.pop_front();
			const double queueMilliseconds = Milliseconds(
				std::chrono::steady_clock::now() - publication.m_QueuedAt);
			m_GpuFinalizeTelemetry.m_TotalQueueMilliseconds += queueMilliseconds;
			++m_GpuFinalizeTelemetry.m_QueueSampleCount;
			m_GpuFinalizeTelemetry.m_MaxQueueMilliseconds = std::max(
				m_GpuFinalizeTelemetry.m_MaxQueueMilliseconds,
				queueMilliseconds);
			const auto executionBegin = std::chrono::steady_clock::now();
			FinishUpload(std::move(publication.m_Upload), publication.m_Status);
			const double executionMilliseconds = Milliseconds(
				std::chrono::steady_clock::now() - executionBegin);
			m_GpuFinalizeTelemetry.m_TotalExecutionMilliseconds += executionMilliseconds;
			m_GpuFinalizeTelemetry.m_MaxExecutionMilliseconds = std::max(
				m_GpuFinalizeTelemetry.m_MaxExecutionMilliseconds,
				executionMilliseconds);
			++processedCount;
			++m_LastFrameUsage.m_GpuFinalizeItems;
			m_LastFrameUsage.m_GpuFinalizeMilliseconds += executionMilliseconds;
			++m_GpuFinalizeTelemetry.m_ProcessedCount;
		}
		return processedCount;
	}

	void AssetUploadScheduler::RemoveReadyPayload(
		const AssetStreamingWorkEstimate& estimate,
		bool uploadRecording) noexcept
	{
		GGLAB_ASSERT_MSG(
			estimate.m_SourceBytes <= m_ReadyPayloadBytes,
			"Asset ready payload byte accounting underflow.");
		m_ReadyPayloadBytes = estimate.m_SourceBytes <= m_ReadyPayloadBytes ?
			m_ReadyPayloadBytes - estimate.m_SourceBytes : 0;
		if (uploadRecording)
		{
			GGLAB_ASSERT_MSG(
				estimate.m_SourceBytes <= m_UploadRecordingBacklogBytes,
				"Asset upload-recording payload byte accounting underflow.");
			m_UploadRecordingBacklogBytes = estimate.m_SourceBytes <= m_UploadRecordingBacklogBytes ?
				m_UploadRecordingBacklogBytes - estimate.m_SourceBytes : 0;
		}
	}

	void AssetUploadScheduler::RetireResourcePublicationPayload(
		QueuedResourcePublication& publication,
		const AssetResourcePublicationStepUsage& usage) noexcept
	{
		const uint64_t movedBytes = usage.m_PayloadBytesMovedToUpload;
		const uint64_t destroyedBytes = usage.m_PayloadBytesDestroyed;
		GGLAB_ASSERT_MSG(
			movedBytes <= publication.m_RemainingSourceBytes &&
			destroyedBytes <= publication.m_RemainingSourceBytes -
				std::min(movedBytes, publication.m_RemainingSourceBytes),
			"Resource publication step retired more payload than the job owns.");
		const uint64_t clampedMovedBytes = std::min(
			movedBytes,
			publication.m_RemainingSourceBytes);
		const uint64_t retiredBytes = clampedMovedBytes + std::min(
			destroyedBytes,
			publication.m_RemainingSourceBytes - clampedMovedBytes);
		GGLAB_ASSERT_MSG(
			retiredBytes <= m_ReadyPayloadBytes,
			"Resource publication ready payload accounting underflow.");
		publication.m_RemainingSourceBytes -= retiredBytes;
		publication.m_Desc.m_Estimate.m_SourceBytes =
			publication.m_RemainingSourceBytes;
		m_ReadyPayloadBytes = retiredBytes <= m_ReadyPayloadBytes ?
			m_ReadyPayloadBytes - retiredBytes : 0;
	}

	uint32_t AssetUploadScheduler::CancelQueuedWork(
		std::deque<QueuedWork>& queue,
		QueueTelemetry& telemetry,
		const AssetStreamingIdentity& identity,
		bool uploadRecording) noexcept
	{
		uint32_t cancelledCount = 0;
		for (auto iterator = queue.begin(); iterator != queue.end();)
		{
			if (iterator->m_Desc.m_Identity != identity)
			{
				++iterator;
				continue;
			}

			RemoveReadyPayload(iterator->m_Desc.m_Estimate, uploadRecording);
			iterator = queue.erase(iterator);
			++cancelledCount;
			++telemetry.m_CancelledCount;
		}
		return cancelledCount;
	}

	uint32_t AssetUploadScheduler::CancelResourcePublication(
		const AssetStreamingIdentity& identity,
		AssetResourcePublicationAbortReason reason) noexcept
	{
		AssetResourcePublicationContext context{
			.m_Scheduler = this,
		};
		uint32_t cancelledCount = 0;
		for (auto iterator = m_ResourcePublicationQueue.begin();
			iterator != m_ResourcePublicationQueue.end();)
		{
			if (iterator->m_Desc.m_Identity != identity)
			{
				++iterator;
				continue;
			}

			iterator->m_Job->Abort(context, reason);
			GGLAB_ASSERT_MSG(
				iterator->m_RemainingSourceBytes <= m_ReadyPayloadBytes,
				"Resource publication cancellation payload accounting underflow.");
			m_ReadyPayloadBytes = iterator->m_RemainingSourceBytes <= m_ReadyPayloadBytes ?
				m_ReadyPayloadBytes - iterator->m_RemainingSourceBytes : 0;
			iterator = m_ResourcePublicationQueue.erase(iterator);
			++cancelledCount;
			++m_ResourcePublicationTelemetry.m_CancelledCount;
		}
		return cancelledCount;
	}

	uint32_t AssetUploadScheduler::UpdateQueuedWorkPriority(
		std::deque<QueuedWork>& queue,
		const AssetStreamingIdentity& identity,
		TaskPriority priority) noexcept
	{
		uint32_t updatedCount = 0;
		for (QueuedWork& work : queue)
		{
			if (work.m_Desc.m_Identity == identity && work.m_Desc.m_Priority != priority)
			{
				work.m_Desc.m_Priority = priority;
				++updatedCount;
			}
		}
		if (updatedCount > 0)
		{
			std::ranges::stable_sort(queue,
				[](const QueuedWork& lhs, const QueuedWork& rhs) noexcept
				{
					return HasHigherPriority(
						lhs.m_Desc.m_Priority,
						rhs.m_Desc.m_Priority);
				});
		}
		return updatedCount;
	}

	uint32_t AssetUploadScheduler::UpdateResourcePublicationPriority(
		const AssetStreamingIdentity& identity,
		TaskPriority priority) noexcept
	{
		uint32_t updatedCount = 0;
		for (QueuedResourcePublication& publication : m_ResourcePublicationQueue)
		{
			if (publication.m_Desc.m_Identity == identity &&
				publication.m_Desc.m_Priority != priority)
			{
				publication.m_Desc.m_Priority = priority;
				++updatedCount;
			}
		}
		if (updatedCount > 0)
		{
			std::ranges::stable_sort(m_ResourcePublicationQueue,
				[](const QueuedResourcePublication& lhs,
					const QueuedResourcePublication& rhs) noexcept
				{
					return HasHigherPriority(
						lhs.m_Desc.m_Priority,
						rhs.m_Desc.m_Priority);
				});
		}
		return updatedCount;
	}

	AssetStreamingQueueStatistics AssetUploadScheduler::BuildQueueStatistics(
		const std::deque<QueuedWork>& queue,
		const QueueTelemetry& telemetry) const
	{
		AssetStreamingQueueStatistics statistics{};
		statistics.m_PendingCount = static_cast<uint32_t>(queue.size());
		statistics.m_HighWatermark = telemetry.m_HighWatermark;
		statistics.m_EnqueuedCount = telemetry.m_EnqueuedCount;
		statistics.m_ProcessedCount = telemetry.m_ProcessedCount;
		statistics.m_ContinueCount = telemetry.m_ContinueCount;
		statistics.m_CompletedCount = telemetry.m_CompletedCount;
		statistics.m_FailedCount = telemetry.m_FailedCount;
		statistics.m_CallbackFailureCount = telemetry.m_CallbackFailureCount;
		statistics.m_CancelledCount = telemetry.m_CancelledCount;
		statistics.m_ResourceCreationCount = telemetry.m_ResourceCreationCount;
		statistics.m_PayloadBytesMovedToUpload = telemetry.m_PayloadBytesMovedToUpload;
		statistics.m_PayloadBytesDestroyed = telemetry.m_PayloadBytesDestroyed;
		statistics.m_QueueSampleCount = telemetry.m_QueueSampleCount;
		statistics.m_TotalQueueMilliseconds = telemetry.m_TotalQueueMilliseconds;
		statistics.m_MaxQueueMilliseconds = telemetry.m_MaxQueueMilliseconds;
		statistics.m_TotalExecutionMilliseconds = telemetry.m_TotalExecutionMilliseconds;
		statistics.m_MaxExecutionMilliseconds = telemetry.m_MaxExecutionMilliseconds;
		statistics.m_ExecutionP95Milliseconds = ExecutionP95(
			telemetry.m_RecentExecutionMilliseconds);
		statistics.m_PendingWork.reserve(queue.size());
		const auto now = std::chrono::steady_clock::now();
		for (const QueuedWork& work : queue)
		{
			statistics.m_PendingSourceBytes += work.m_Desc.m_Estimate.m_SourceBytes;
			statistics.m_PendingStagingBytes += work.m_Desc.m_Estimate.m_StagingBytes;
			statistics.m_PendingOperationCount += work.m_Desc.m_Estimate.m_OperationCount;
			statistics.m_PendingWork.push_back({
				.m_Name = work.m_Desc.m_Name,
				.m_Identity = work.m_Desc.m_Identity,
				.m_Estimate = work.m_Desc.m_Estimate,
				.m_Priority = work.m_Desc.m_Priority,
				.m_QueueMilliseconds = Milliseconds(now - work.m_QueuedAt),
				.m_Progress = work.m_Desc.m_Progress ?
					work.m_Desc.m_Progress->GetSnapshot() : ProgressSnapshot{},
			});
		}
		return statistics;
	}

	AssetStreamingQueueStatistics AssetUploadScheduler::BuildResourcePublicationQueueStatistics() const
	{
		GGLAB_ASSERT_MSG(
			m_ResourcePublicationTelemetry.m_EnqueuedCount ==
				m_ResourcePublicationTelemetry.m_CompletedCount +
				m_ResourcePublicationTelemetry.m_FailedCount +
				m_ResourcePublicationTelemetry.m_CancelledCount +
				m_ResourcePublicationQueue.size(),
			"Resource publication terminal accounting invariant failed.");
		AssetStreamingQueueStatistics statistics{};
		statistics.m_PendingCount = static_cast<uint32_t>(m_ResourcePublicationQueue.size());
		statistics.m_HighWatermark = m_ResourcePublicationTelemetry.m_HighWatermark;
		statistics.m_EnqueuedCount = m_ResourcePublicationTelemetry.m_EnqueuedCount;
		statistics.m_ProcessedCount = m_ResourcePublicationTelemetry.m_ProcessedCount;
		statistics.m_ContinueCount = m_ResourcePublicationTelemetry.m_ContinueCount;
		statistics.m_CompletedCount = m_ResourcePublicationTelemetry.m_CompletedCount;
		statistics.m_FailedCount = m_ResourcePublicationTelemetry.m_FailedCount;
		statistics.m_CallbackFailureCount = m_ResourcePublicationTelemetry.m_CallbackFailureCount;
		statistics.m_CancelledCount = m_ResourcePublicationTelemetry.m_CancelledCount;
		statistics.m_ResourceCreationCount = m_ResourcePublicationTelemetry.m_ResourceCreationCount;
		statistics.m_PayloadBytesMovedToUpload =
			m_ResourcePublicationTelemetry.m_PayloadBytesMovedToUpload;
		statistics.m_PayloadBytesDestroyed =
			m_ResourcePublicationTelemetry.m_PayloadBytesDestroyed;
		statistics.m_QueueSampleCount = m_ResourcePublicationTelemetry.m_QueueSampleCount;
		statistics.m_TotalQueueMilliseconds =
			m_ResourcePublicationTelemetry.m_TotalQueueMilliseconds;
		statistics.m_MaxQueueMilliseconds =
			m_ResourcePublicationTelemetry.m_MaxQueueMilliseconds;
		statistics.m_TotalExecutionMilliseconds =
			m_ResourcePublicationTelemetry.m_TotalExecutionMilliseconds;
		statistics.m_MaxExecutionMilliseconds =
			m_ResourcePublicationTelemetry.m_MaxExecutionMilliseconds;
		statistics.m_ExecutionP95Milliseconds = ExecutionP95(
			m_ResourcePublicationTelemetry.m_RecentExecutionMilliseconds);
		statistics.m_OverBudgetExecutionCount =
			m_PublicationOverBudgetExecutionCount;
		statistics.m_NoProgressContinueCount =
			m_PublicationNoProgressContinueCount;
		statistics.m_FaultInjectionCount =
			m_PublicationFaultInjectionCount;
		for (size_t stageIndex = 0;
			stageIndex < m_PublicationStageTelemetry.size();
			++stageIndex)
		{
			const PublicationStageTelemetry& source =
				m_PublicationStageTelemetry[stageIndex];
			AssetResourcePublicationStageStatistics& destination =
				statistics.m_PublicationStages[stageIndex];
			destination.m_StepCount = source.m_StepCount;
			destination.m_TotalMilliseconds = source.m_TotalMilliseconds;
			destination.m_MaxMilliseconds = source.m_MaxMilliseconds;
			destination.m_P95Milliseconds = ExecutionP95(
				source.m_RecentExecutionMilliseconds);
		}
		statistics.m_PendingWork.reserve(m_ResourcePublicationQueue.size());
		const auto now = std::chrono::steady_clock::now();
		for (const QueuedResourcePublication& publication : m_ResourcePublicationQueue)
		{
			statistics.m_PendingSourceBytes += publication.m_RemainingSourceBytes;
			statistics.m_PendingWork.push_back({
				.m_Name = publication.m_Desc.m_Name,
				.m_Identity = publication.m_Desc.m_Identity,
				.m_Estimate = publication.m_Desc.m_Estimate,
				.m_Priority = publication.m_Desc.m_Priority,
				.m_QueueMilliseconds = Milliseconds(now - publication.m_QueuedAt),
				.m_Progress = publication.m_Desc.m_Progress ?
					publication.m_Desc.m_Progress->GetSnapshot() : ProgressSnapshot{},
			});
		}
		return statistics;
	}

	AssetStreamingQueueStatistics AssetUploadScheduler::BuildGpuFinalizeQueueStatistics() const
	{
		AssetStreamingQueueStatistics statistics{};
		statistics.m_PendingCount = static_cast<uint32_t>(m_GpuFinalizeQueue.size());
		statistics.m_HighWatermark = m_GpuFinalizeTelemetry.m_HighWatermark;
		statistics.m_EnqueuedCount = m_GpuFinalizeTelemetry.m_EnqueuedCount;
		statistics.m_ProcessedCount = m_GpuFinalizeTelemetry.m_ProcessedCount;
		statistics.m_CallbackFailureCount = m_CompletionCallbackFailureCount;
		statistics.m_CancelledCount = m_GpuFinalizeTelemetry.m_CancelledCount;
		statistics.m_QueueSampleCount = m_GpuFinalizeTelemetry.m_QueueSampleCount;
		statistics.m_TotalQueueMilliseconds = m_GpuFinalizeTelemetry.m_TotalQueueMilliseconds;
		statistics.m_MaxQueueMilliseconds = m_GpuFinalizeTelemetry.m_MaxQueueMilliseconds;
		statistics.m_TotalExecutionMilliseconds = m_GpuFinalizeTelemetry.m_TotalExecutionMilliseconds;
		statistics.m_MaxExecutionMilliseconds = m_GpuFinalizeTelemetry.m_MaxExecutionMilliseconds;
		statistics.m_PendingWork.reserve(m_GpuFinalizeQueue.size());
		const auto now = std::chrono::steady_clock::now();
		for (const PendingGpuFinalize& publication : m_GpuFinalizeQueue)
		{
			statistics.m_PendingSourceBytes += publication.m_Upload.m_Desc.m_Estimate.m_SourceBytes;
			statistics.m_PendingStagingBytes += publication.m_Upload.m_Desc.m_Estimate.m_StagingBytes;
			statistics.m_PendingOperationCount += publication.m_Upload.m_Desc.m_Estimate.m_OperationCount;
			statistics.m_PendingWork.push_back({
				.m_Name = publication.m_Upload.m_Desc.m_Name,
				.m_Identity = publication.m_Upload.m_Desc.m_Identity,
				.m_Estimate = publication.m_Upload.m_Desc.m_Estimate,
				.m_Priority = publication.m_Upload.m_Desc.m_Priority,
				.m_QueueMilliseconds = Milliseconds(now - publication.m_QueuedAt),
				.m_Progress = publication.m_Upload.m_Desc.m_Progress ?
					publication.m_Upload.m_Desc.m_Progress->GetSnapshot() : ProgressSnapshot{},
			});
		}
		return statistics;
	}

	void AssetUploadScheduler::FinishUpload(
		PendingUpload&& upload,
		AssetUploadStatus status) noexcept
	{
		ProgressReporter(upload.m_Desc.m_Progress).Report(
			status == AssetUploadStatus::Succeeded ? 0.96f : 0.82f,
			status == AssetUploadStatus::Succeeded ?
				"Publishing GPU resource" : "GPU upload failed",
			upload.m_Desc.m_Name);

		AssetUploadCompletionInfo info{};
		info.m_Handle = upload.m_Handle;
		info.m_Name = std::move(upload.m_Desc.m_Name);
		info.m_Identity = upload.m_Desc.m_Identity;
		info.m_Status = status;
		info.m_FencePoint = upload.m_FencePoint;
		info.m_ElapsedMilliseconds = Milliseconds(
			std::chrono::steady_clock::now() - upload.m_SubmittedAt);

		if (status == AssetUploadStatus::Succeeded)
		{
			++m_SucceededCount;
		}
		else
		{
			++m_FailedCount;
		}

		if (upload.m_Completion)
		{
			try
			{
				upload.m_Completion(info);
			}
			catch (const std::exception& exception)
			{
				GGLAB_UNUSED(exception);
				++m_CompletionCallbackFailureCount;
				GGLAB_LOG_GRAPHICS_ERROR(
					"Asset upload completion '{}' threw an exception: {}",
					info.m_Name,
					exception.what());
			}
			catch (...)
			{
				++m_CompletionCallbackFailureCount;
				GGLAB_LOG_GRAPHICS_ERROR(
					"Asset upload completion '{}' threw an unknown exception.",
					info.m_Name);
			}
		}

		if (m_RecentUploadCapacity > 0)
		{
			const ProgressSnapshot progress = upload.m_Desc.m_Progress ?
				upload.m_Desc.m_Progress->GetSnapshot() : ProgressSnapshot{};
			m_RecentUploads.push_front({
				.m_Handle = info.m_Handle,
				.m_Name = std::move(info.m_Name),
				.m_Identity = upload.m_Desc.m_Identity,
				.m_Estimate = upload.m_Desc.m_Estimate,
				.m_Status = info.m_Status,
				.m_FencePoint = info.m_FencePoint,
				.m_ElapsedMilliseconds = info.m_ElapsedMilliseconds,
				.m_Progress = progress,
			});
			while (m_RecentUploads.size() > m_RecentUploadCapacity)
			{
				m_RecentUploads.pop_back();
			}
		}
	}
}
