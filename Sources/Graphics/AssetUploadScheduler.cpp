#include "Core/Precompiled.h"
#include "Graphics/AssetUploadScheduler.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/TransferBatch.h"
#include "Graphics/TransferManager.h"

namespace gglab
{
	namespace
	{
		template<typename Rep, typename Period>
		[[nodiscard]] double Milliseconds(std::chrono::duration<Rep, Period> duration) noexcept
		{
			return std::chrono::duration<double, std::milli>(duration).count();
		}
	}

	AssetUploadScheduler::AssetUploadScheduler(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_TransferManager(createInfo.m_TransferManager),
		m_OwnerThreadId(std::this_thread::get_id()),
		m_RecentUploadCapacity(createInfo.m_RecentUploadCapacity)
	{
		GGLAB_ASSERT_NOT_NULL(m_Device);
		GGLAB_ASSERT_NOT_NULL(m_TransferManager);
	}

	AssetUploadScheduler::~AssetUploadScheduler()
	{
		GGLAB_ASSERT_MSG(
			m_CpuReadyQueue.empty() &&
			m_UploadReadyQueue.empty() &&
			m_PendingUploads.empty() &&
			m_PublicationReadyQueue.empty(),
			"AssetUploadScheduler destroyed with pending streaming work. Call Finalize after draining ready work and waiting for GPU idle.");
	}

	void AssetUploadScheduler::EnqueueCpuReady(
		AssetStreamingWorkDesc desc,
		AssetStreamingWork work) noexcept
	{
		EnqueueWork(
			m_CpuReadyQueue,
			m_CpuReadyTelemetry,
			std::move(desc),
			std::move(work));
	}

	void AssetUploadScheduler::EnqueueUploadReady(
		AssetStreamingWorkDesc desc,
		AssetStreamingWork work) noexcept
	{
		EnqueueWork(
			m_UploadReadyQueue,
			m_UploadReadyTelemetry,
			std::move(desc),
			std::move(work));
	}

	AssetUploadHandle AssetUploadScheduler::Submit(
		AssetUploadDesc desc,
		TransferBatch&& batch,
		bool recordingSucceeded,
		AssetUploadCompletion completion) noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "Asset uploads must be submitted on the scheduler owner thread.");
		if (!IsOwnerThread())
		{
			return {};
		}

		PendingUpload upload{};
		upload.m_Handle = { m_NextHandle++ };
		upload.m_Desc = std::move(desc);
		upload.m_SubmittedAt = std::chrono::steady_clock::now();
		upload.m_RecordingSucceeded = recordingSucceeded;
		upload.m_Completion = std::move(completion);
		upload.m_FencePoint = batch.Submit(false);
		++m_SubmittedCount;

		if (!upload.m_FencePoint.IsValid())
		{
			ProgressReporter(upload.m_Desc.m_Progress).Report(
				0.72f,
				"GPU upload submission failed",
				upload.m_Desc.m_Name);
			EnqueuePublication(std::move(upload), AssetUploadStatus::Failed);
			return {};
		}

		const AssetUploadHandle handle = upload.m_Handle;
		ProgressReporter(upload.m_Desc.m_Progress).Report(
			0.82f,
			"Waiting for GPU upload",
			std::format("{} | fence {}", upload.m_Desc.m_Name, upload.m_FencePoint.m_Value));
		m_PendingUploads.emplace_back(std::move(upload));
		return handle;
	}

	uint32_t AssetUploadScheduler::Tick() noexcept
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "Asset upload fences must be polled on the scheduler owner thread.");
		if (!IsOwnerThread())
		{
			return 0;
		}

		GGLAB_UNUSED(DrainWorkQueue(
			m_CpuReadyQueue,
			m_CpuReadyTelemetry,
			"CPU-ready"));
		GGLAB_UNUSED(DrainWorkQueue(
			m_UploadReadyQueue,
			m_UploadReadyTelemetry,
			"upload-ready"));
		GGLAB_UNUSED(PollCompletedUploads());
		return DrainPublicationQueue();
	}

	uint32_t AssetUploadScheduler::PollCompletedUploads() noexcept
	{
		std::vector<PendingUpload> completed;
		for (auto iterator = m_PendingUploads.begin(); iterator != m_PendingUploads.end();)
		{
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
			const AssetUploadStatus status = upload.m_RecordingSucceeded ?
				AssetUploadStatus::Succeeded : AssetUploadStatus::Failed;
			EnqueuePublication(std::move(upload), status);
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

		GGLAB_ASSERT_MSG(
			m_CpuReadyQueue.empty() && m_UploadReadyQueue.empty(),
			"AssetUploadScheduler::Finalize requires ready work to be drained before the RHI context becomes idle.");
		GGLAB_UNUSED(PollCompletedUploads());
		GGLAB_UNUSED(DrainPublicationQueue());
		GGLAB_ASSERT_MSG(
			m_PendingUploads.empty(),
			"AssetUploadScheduler::Finalize requires the RHI context to be idle.");
		while (!m_PendingUploads.empty())
		{
			PendingUpload upload = std::move(m_PendingUploads.front());
			m_PendingUploads.pop_front();
			EnqueuePublication(std::move(upload), AssetUploadStatus::Failed);
		}
		GGLAB_UNUSED(DrainPublicationQueue());
		m_TransferManager->Reclaim();
	}

	AssetUploadStatistics AssetUploadScheduler::GetStatistics() const
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "AssetUploadScheduler statistics must be read on the owner thread.");
		AssetUploadStatistics statistics{};
		statistics.m_CpuReadyQueue = BuildQueueStatistics(
			m_CpuReadyQueue,
			m_CpuReadyTelemetry);
		statistics.m_UploadReadyQueue = BuildQueueStatistics(
			m_UploadReadyQueue,
			m_UploadReadyTelemetry);
		statistics.m_PublicationReadyQueue = BuildPublicationQueueStatistics();
		statistics.m_PendingCount = static_cast<uint32_t>(m_PendingUploads.size());
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

		queue.push_back({
			.m_Desc = std::move(desc),
			.m_QueuedAt = std::chrono::steady_clock::now(),
			.m_Work = std::move(work),
		});
		++telemetry.m_EnqueuedCount;
		telemetry.m_HighWatermark = std::max(
			telemetry.m_HighWatermark,
			static_cast<uint32_t>(queue.size()));
	}

	uint32_t AssetUploadScheduler::DrainWorkQueue(
		std::deque<QueuedWork>& queue,
		QueueTelemetry& telemetry,
		std::string_view queueName) noexcept
	{
		const uint32_t initialCount = static_cast<uint32_t>(queue.size());
		uint32_t processedCount = 0;
		while (processedCount < initialCount && !queue.empty())
		{
			QueuedWork queued = std::move(queue.front());
			queue.pop_front();
			const double queueMilliseconds = Milliseconds(
				std::chrono::steady_clock::now() - queued.m_QueuedAt);
			telemetry.m_TotalQueueMilliseconds += queueMilliseconds;
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

			++processedCount;
			++telemetry.m_ProcessedCount;
		}
		return processedCount;
	}

	void AssetUploadScheduler::EnqueuePublication(
		PendingUpload&& upload,
		AssetUploadStatus status) noexcept
	{
		m_PublicationReadyQueue.push_back({
			.m_Upload = std::move(upload),
			.m_Status = status,
			.m_QueuedAt = std::chrono::steady_clock::now(),
		});
		++m_PublicationReadyTelemetry.m_EnqueuedCount;
		m_PublicationReadyTelemetry.m_HighWatermark = std::max(
			m_PublicationReadyTelemetry.m_HighWatermark,
			static_cast<uint32_t>(m_PublicationReadyQueue.size()));
	}

	uint32_t AssetUploadScheduler::DrainPublicationQueue() noexcept
	{
		const uint32_t initialCount = static_cast<uint32_t>(m_PublicationReadyQueue.size());
		uint32_t processedCount = 0;
		while (processedCount < initialCount && !m_PublicationReadyQueue.empty())
		{
			PendingPublication publication = std::move(m_PublicationReadyQueue.front());
			m_PublicationReadyQueue.pop_front();
			const double queueMilliseconds = Milliseconds(
				std::chrono::steady_clock::now() - publication.m_QueuedAt);
			m_PublicationReadyTelemetry.m_TotalQueueMilliseconds += queueMilliseconds;
			m_PublicationReadyTelemetry.m_MaxQueueMilliseconds = std::max(
				m_PublicationReadyTelemetry.m_MaxQueueMilliseconds,
				queueMilliseconds);
			const auto executionBegin = std::chrono::steady_clock::now();
			FinishUpload(std::move(publication.m_Upload), publication.m_Status);
			const double executionMilliseconds = Milliseconds(
				std::chrono::steady_clock::now() - executionBegin);
			m_PublicationReadyTelemetry.m_TotalExecutionMilliseconds += executionMilliseconds;
			m_PublicationReadyTelemetry.m_MaxExecutionMilliseconds = std::max(
				m_PublicationReadyTelemetry.m_MaxExecutionMilliseconds,
				executionMilliseconds);
			++processedCount;
			++m_PublicationReadyTelemetry.m_ProcessedCount;
		}
		return processedCount;
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
		statistics.m_CallbackFailureCount = telemetry.m_CallbackFailureCount;
		statistics.m_TotalQueueMilliseconds = telemetry.m_TotalQueueMilliseconds;
		statistics.m_MaxQueueMilliseconds = telemetry.m_MaxQueueMilliseconds;
		statistics.m_TotalExecutionMilliseconds = telemetry.m_TotalExecutionMilliseconds;
		statistics.m_MaxExecutionMilliseconds = telemetry.m_MaxExecutionMilliseconds;
		statistics.m_PendingWork.reserve(queue.size());
		const auto now = std::chrono::steady_clock::now();
		for (const QueuedWork& work : queue)
		{
			statistics.m_PendingWork.push_back({
				.m_Name = work.m_Desc.m_Name,
				.m_Identity = work.m_Desc.m_Identity,
				.m_QueueMilliseconds = Milliseconds(now - work.m_QueuedAt),
				.m_Progress = work.m_Desc.m_Progress ?
					work.m_Desc.m_Progress->GetSnapshot() : ProgressSnapshot{},
			});
		}
		return statistics;
	}

	AssetStreamingQueueStatistics AssetUploadScheduler::BuildPublicationQueueStatistics() const
	{
		AssetStreamingQueueStatistics statistics{};
		statistics.m_PendingCount = static_cast<uint32_t>(m_PublicationReadyQueue.size());
		statistics.m_HighWatermark = m_PublicationReadyTelemetry.m_HighWatermark;
		statistics.m_EnqueuedCount = m_PublicationReadyTelemetry.m_EnqueuedCount;
		statistics.m_ProcessedCount = m_PublicationReadyTelemetry.m_ProcessedCount;
		statistics.m_CallbackFailureCount = m_CompletionCallbackFailureCount;
		statistics.m_TotalQueueMilliseconds = m_PublicationReadyTelemetry.m_TotalQueueMilliseconds;
		statistics.m_MaxQueueMilliseconds = m_PublicationReadyTelemetry.m_MaxQueueMilliseconds;
		statistics.m_TotalExecutionMilliseconds = m_PublicationReadyTelemetry.m_TotalExecutionMilliseconds;
		statistics.m_MaxExecutionMilliseconds = m_PublicationReadyTelemetry.m_MaxExecutionMilliseconds;
		statistics.m_PendingWork.reserve(m_PublicationReadyQueue.size());
		const auto now = std::chrono::steady_clock::now();
		for (const PendingPublication& publication : m_PublicationReadyQueue)
		{
			statistics.m_PendingWork.push_back({
				.m_Name = publication.m_Upload.m_Desc.m_Name,
				.m_Identity = publication.m_Upload.m_Desc.m_Identity,
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
