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
			m_PendingUploads.empty(),
			"AssetUploadScheduler destroyed with pending GPU uploads. Call Finalize after waiting for GPU idle.");
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
			FinishUpload(std::move(upload), AssetUploadStatus::Failed);
			return {};
		}

		const AssetUploadHandle handle = upload.m_Handle;
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
			FinishUpload(
				std::move(upload),
				status);
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

		Tick();
		GGLAB_ASSERT_MSG(
			m_PendingUploads.empty(),
			"AssetUploadScheduler::Finalize requires the RHI context to be idle.");
		while (!m_PendingUploads.empty())
		{
			PendingUpload upload = std::move(m_PendingUploads.front());
			m_PendingUploads.pop_front();
			FinishUpload(std::move(upload), AssetUploadStatus::Failed);
		}
		m_TransferManager->Reclaim();
	}

	AssetUploadStatistics AssetUploadScheduler::GetStatistics() const
	{
		GGLAB_ASSERT_MSG(IsOwnerThread(), "AssetUploadScheduler statistics must be read on the owner thread.");
		AssetUploadStatistics statistics{};
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
			statistics.m_PendingUploads.push_back({
				.m_Handle = upload.m_Handle,
				.m_Name = upload.m_Desc.m_Name,
				.m_Status = AssetUploadStatus::Pending,
				.m_FencePoint = upload.m_FencePoint,
				.m_ElapsedMilliseconds = Milliseconds(now - upload.m_SubmittedAt),
			});
		}
		return statistics;
	}

	bool AssetUploadScheduler::IsOwnerThread() const noexcept
	{
		return std::this_thread::get_id() == m_OwnerThreadId;
	}

	void AssetUploadScheduler::FinishUpload(
		PendingUpload&& upload,
		AssetUploadStatus status) noexcept
	{
		AssetUploadCompletionInfo info{};
		info.m_Handle = upload.m_Handle;
		info.m_Name = std::move(upload.m_Desc.m_Name);
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
			m_RecentUploads.push_front({
				.m_Handle = info.m_Handle,
				.m_Name = std::move(info.m_Name),
				.m_Status = info.m_Status,
				.m_FencePoint = info.m_FencePoint,
				.m_ElapsedMilliseconds = info.m_ElapsedMilliseconds,
			});
			while (m_RecentUploads.size() > m_RecentUploadCapacity)
			{
				m_RecentUploads.pop_back();
			}
		}
	}
}
