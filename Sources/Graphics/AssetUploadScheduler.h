#pragma once
#include "Core/Async/ProgressChannel.h"
#include "Core/Task/TaskTypes.h"
#include "Graphics/RHI/RHIFence.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gglab
{
	class RHIDevice;
	class TransferBatch;
	class TransferManager;

	struct AssetUploadHandle
	{
		uint64_t m_Value = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return m_Value != 0; }
		explicit constexpr operator bool() const noexcept { return IsValid(); }
		friend constexpr auto operator<=>(const AssetUploadHandle&, const AssetUploadHandle&) = default;
	};

	enum class AssetUploadStatus : uint8_t
	{
		Pending,
		Succeeded,
		Failed,
	};

	enum class AssetStreamingWorkKind : uint8_t
	{
		Unknown,
		Model,
		Texture,
		Mesh,
	};

	struct AssetStreamingIdentity
	{
		AssetStreamingWorkKind m_Kind = AssetStreamingWorkKind::Unknown;
		uint64_t m_StableId = 0;
		uint64_t m_Generation = 0;

		friend constexpr bool operator==(
			const AssetStreamingIdentity&,
			const AssetStreamingIdentity&) = default;
	};

	struct AssetStreamingWorkEstimate
	{
		uint64_t m_SourceBytes = 0;
		uint64_t m_StagingBytes = 0;
		uint32_t m_OperationCount = 0;
	};

	struct AssetStreamingFrameBudget
	{
		uint32_t m_MaxCpuReadyItems = 8;
		double m_MaxCpuReadyMilliseconds = 0.5;
		uint32_t m_MaxUploadReadyItems = 8;
		uint64_t m_MaxUploadBytes = 32ull * 1024ull * 1024ull;
		uint32_t m_MaxUploadOperations = 64;
		double m_MaxUploadMilliseconds = 1.0;
		uint32_t m_MaxPublicationItems = 16;
		double m_MaxPublicationMilliseconds = 0.5;
		uint64_t m_MaxInFlightBytes = 256ull * 1024ull * 1024ull;
		uint64_t m_MaxReadyBacklogBytes = 512ull * 1024ull * 1024ull;
	};

	struct AssetStreamingFrameUsage
	{
		uint32_t m_CpuReadyItems = 0;
		double m_CpuReadyMilliseconds = 0.0;
		uint32_t m_UploadReadyItems = 0;
		uint64_t m_UploadBytes = 0;
		uint32_t m_UploadOperations = 0;
		double m_UploadMilliseconds = 0.0;
		uint32_t m_PublicationItems = 0;
		double m_PublicationMilliseconds = 0.0;
	};

	struct AssetStreamingWorkDesc
	{
		std::string m_Name;
		AssetStreamingIdentity m_Identity{};
		AssetStreamingWorkEstimate m_Estimate{};
		TaskPriority m_Priority = TaskPriority::Normal;
		ProgressChannelPtr m_Progress;
	};

	using AssetStreamingWork = std::function<void()>;

	struct AssetStreamingWorkActivity
	{
		std::string m_Name;
		AssetStreamingIdentity m_Identity{};
		AssetStreamingWorkEstimate m_Estimate{};
		TaskPriority m_Priority = TaskPriority::Normal;
		double m_QueueMilliseconds = 0.0;
		ProgressSnapshot m_Progress;
	};

	struct AssetStreamingQueueStatistics
	{
		uint32_t m_PendingCount = 0;
		uint32_t m_HighWatermark = 0;
		uint64_t m_EnqueuedCount = 0;
		uint64_t m_ProcessedCount = 0;
		uint64_t m_CallbackFailureCount = 0;
		uint64_t m_CancelledCount = 0;
		uint64_t m_PendingSourceBytes = 0;
		uint64_t m_PendingStagingBytes = 0;
		uint64_t m_PendingOperationCount = 0;
		double m_TotalQueueMilliseconds = 0.0;
		double m_MaxQueueMilliseconds = 0.0;
		double m_TotalExecutionMilliseconds = 0.0;
		double m_MaxExecutionMilliseconds = 0.0;
		std::vector<AssetStreamingWorkActivity> m_PendingWork;
	};

	struct AssetUploadDesc
	{
		std::string m_Name;
		AssetStreamingIdentity m_Identity{};
		AssetStreamingWorkEstimate m_Estimate{};
		TaskPriority m_Priority = TaskPriority::Normal;
		ProgressChannelPtr m_Progress;
	};

	struct AssetUploadCompletionInfo
	{
		AssetUploadHandle m_Handle{};
		std::string m_Name;
		AssetStreamingIdentity m_Identity{};
		AssetUploadStatus m_Status = AssetUploadStatus::Failed;
		RHIFencePoint m_FencePoint{};
		double m_ElapsedMilliseconds = 0.0;
	};

	using AssetUploadCompletion = std::function<void(const AssetUploadCompletionInfo&)>;

	struct AssetUploadActivity
	{
		AssetUploadHandle m_Handle{};
		std::string m_Name;
		AssetStreamingIdentity m_Identity{};
		AssetStreamingWorkEstimate m_Estimate{};
		AssetUploadStatus m_Status = AssetUploadStatus::Pending;
		RHIFencePoint m_FencePoint{};
		double m_ElapsedMilliseconds = 0.0;
		ProgressSnapshot m_Progress;
	};

	struct AssetUploadStatistics
	{
		AssetStreamingQueueStatistics m_CpuReadyQueue;
		AssetStreamingQueueStatistics m_UploadReadyQueue;
		AssetStreamingQueueStatistics m_PublicationReadyQueue;
		AssetStreamingFrameBudget m_FrameBudget;
		AssetStreamingFrameUsage m_LastFrameUsage;
		uint64_t m_ReadyBacklogBytes = 0;
		uint64_t m_ReadyBacklogHighWatermark = 0;
		uint64_t m_InFlightBytes = 0;
		uint64_t m_InFlightHighWatermark = 0;
		uint64_t m_BacklogBudgetDeferralCount = 0;
		uint64_t m_UploadBudgetDeferralCount = 0;
		uint64_t m_InFlightBudgetDeferralCount = 0;
		uint64_t m_OversizedAdmissionCount = 0;
		uint32_t m_PendingCount = 0;
		uint64_t m_SubmittedCount = 0;
		uint64_t m_SucceededCount = 0;
		uint64_t m_FailedCount = 0;
		uint64_t m_CompletionCallbackFailureCount = 0;
		std::vector<AssetUploadActivity> m_PendingUploads;
		std::vector<AssetUploadActivity> m_RecentUploads;
	};

	// Owns the main-thread streaming boundaries around transfer-queue work. CPU
	// completions only enqueue payload publication, upload-ready work records and
	// submits transfer batches, and completed fences enqueue final publication.
	class AssetUploadScheduler
	{
	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			TransferManager* m_TransferManager = nullptr;
			uint32_t m_RecentUploadCapacity = 64;
			AssetStreamingFrameBudget m_FrameBudget{};
		};

		explicit AssetUploadScheduler(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(AssetUploadScheduler);
		~AssetUploadScheduler();

		void EnqueueCpuReady(
			AssetStreamingWorkDesc desc,
			AssetStreamingWork work) noexcept;
		void EnqueueUploadReady(
			AssetStreamingWorkDesc desc,
			AssetStreamingWork work) noexcept;
		uint32_t CancelReadyWork(const AssetStreamingIdentity& identity) noexcept;
		uint32_t UpdateWorkPriority(
			const AssetStreamingIdentity& identity,
			TaskPriority priority) noexcept;

		[[nodiscard]] AssetUploadHandle Submit(
			AssetUploadDesc desc,
			TransferBatch&& batch,
			bool recordingSucceeded,
			AssetUploadCompletion completion = {}) noexcept;
		uint32_t Tick() noexcept;
		void DrainReadyWork() noexcept;
		void Finalize() noexcept;

		[[nodiscard]] AssetUploadStatistics GetStatistics() const;

	private:
		struct PendingUpload
		{
			AssetUploadHandle m_Handle{};
			AssetUploadDesc m_Desc;
			RHIFencePoint m_FencePoint{};
			std::chrono::steady_clock::time_point m_SubmittedAt{};
			bool m_RecordingSucceeded = false;
			AssetUploadCompletion m_Completion;
		};

		struct QueuedWork
		{
			AssetStreamingWorkDesc m_Desc;
			std::chrono::steady_clock::time_point m_QueuedAt{};
			AssetStreamingWork m_Work;
		};

		struct PendingPublication
		{
			PendingUpload m_Upload;
			AssetUploadStatus m_Status = AssetUploadStatus::Failed;
			std::chrono::steady_clock::time_point m_QueuedAt{};
		};

		struct QueueTelemetry
		{
			uint32_t m_HighWatermark = 0;
			uint64_t m_EnqueuedCount = 0;
			uint64_t m_ProcessedCount = 0;
			uint64_t m_CallbackFailureCount = 0;
			uint64_t m_CancelledCount = 0;
			double m_TotalQueueMilliseconds = 0.0;
			double m_MaxQueueMilliseconds = 0.0;
			double m_TotalExecutionMilliseconds = 0.0;
			double m_MaxExecutionMilliseconds = 0.0;
		};

		[[nodiscard]] bool IsOwnerThread() const noexcept;
		void EnqueueWork(
			std::deque<QueuedWork>& queue,
			QueueTelemetry& telemetry,
			AssetStreamingWorkDesc desc,
			AssetStreamingWork work) noexcept;
		[[nodiscard]] double ExecuteWork(
			QueuedWork&& queued,
			QueueTelemetry& telemetry,
			std::string_view queueName) noexcept;
		uint32_t DrainCpuReadyQueue(bool ignoreBudget) noexcept;
		uint32_t DrainUploadReadyQueue(bool ignoreBudget) noexcept;
		uint32_t PollCompletedUploads() noexcept;
		void EnqueuePublication(
			PendingUpload&& upload,
			AssetUploadStatus status) noexcept;
		uint32_t DrainPublicationQueue(bool ignoreBudget) noexcept;
		void RemoveReadyBacklog(const AssetStreamingWorkEstimate& estimate, bool uploadReady) noexcept;
		uint32_t CancelQueuedWork(
			std::deque<QueuedWork>& queue,
			QueueTelemetry& telemetry,
			const AssetStreamingIdentity& identity,
			bool uploadReady) noexcept;
		uint32_t UpdateQueuedWorkPriority(
			std::deque<QueuedWork>& queue,
			const AssetStreamingIdentity& identity,
			TaskPriority priority) noexcept;
		[[nodiscard]] AssetStreamingQueueStatistics BuildQueueStatistics(
			const std::deque<QueuedWork>& queue,
			const QueueTelemetry& telemetry) const;
		[[nodiscard]] AssetStreamingQueueStatistics BuildPublicationQueueStatistics() const;
		void FinishUpload(PendingUpload&& upload, AssetUploadStatus status) noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		TransferManager* m_TransferManager = nullptr;
		std::thread::id m_OwnerThreadId;
		uint32_t m_RecentUploadCapacity = 0;
		AssetStreamingFrameBudget m_FrameBudget{};
		AssetStreamingFrameUsage m_LastFrameUsage{};
		uint64_t m_NextHandle = 1;
		uint64_t m_ReadyBacklogBytes = 0;
		uint64_t m_UploadReadyBacklogBytes = 0;
		uint64_t m_ReadyBacklogHighWatermark = 0;
		uint64_t m_InFlightBytes = 0;
		uint64_t m_InFlightHighWatermark = 0;
		uint64_t m_BacklogBudgetDeferralCount = 0;
		uint64_t m_UploadBudgetDeferralCount = 0;
		uint64_t m_InFlightBudgetDeferralCount = 0;
		uint64_t m_OversizedAdmissionCount = 0;
		uint64_t m_SubmittedCount = 0;
		uint64_t m_SucceededCount = 0;
		uint64_t m_FailedCount = 0;
		uint64_t m_CompletionCallbackFailureCount = 0;
		std::deque<QueuedWork> m_CpuReadyQueue;
		std::deque<QueuedWork> m_UploadReadyQueue;
		std::deque<PendingUpload> m_PendingUploads;
		std::deque<PendingPublication> m_PublicationReadyQueue;
		std::deque<AssetUploadActivity> m_RecentUploads;
		QueueTelemetry m_CpuReadyTelemetry;
		QueueTelemetry m_UploadReadyTelemetry;
		QueueTelemetry m_PublicationReadyTelemetry;
	};
}
