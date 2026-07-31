#pragma once
#include "Core/Async/ProgressChannel.h"
#include "Core/Task/TaskTypes.h"
#include "Graphics/Asset/AssetIdentity.h"
#include "Graphics/Asset/Publication/AssetResourcePublication.h"
#include "Graphics/RHI/RHIFence.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
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
		friend constexpr auto operator<=>(
			const AssetUploadHandle&, const AssetUploadHandle&) = default;
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
			const AssetStreamingIdentity&, const AssetStreamingIdentity&) = default;
	};

	[[nodiscard]] constexpr AssetKind ToAssetKind(AssetStreamingWorkKind kind) noexcept
	{
		switch (kind)
		{
		case AssetStreamingWorkKind::Model:
			return AssetKind::Model;
		case AssetStreamingWorkKind::Texture:
			return AssetKind::Texture;
		case AssetStreamingWorkKind::Mesh:
			return AssetKind::Mesh;
		case AssetStreamingWorkKind::Unknown:
			return AssetKind::Unknown;
		}
		return AssetKind::Unknown;
	}

	[[nodiscard]] constexpr AssetStreamingWorkKind ToAssetStreamingWorkKind(AssetKind kind) noexcept
	{
		switch (kind)
		{
		case AssetKind::Model:
			return AssetStreamingWorkKind::Model;
		case AssetKind::Texture:
			return AssetStreamingWorkKind::Texture;
		case AssetKind::Mesh:
			return AssetStreamingWorkKind::Mesh;
		case AssetKind::Unknown:
		case AssetKind::Material:
			return AssetStreamingWorkKind::Unknown;
		}
		return AssetStreamingWorkKind::Unknown;
	}

	[[nodiscard]] constexpr AssetContentVersion ToAssetContentVersion(
		const AssetStreamingIdentity& identity) noexcept
	{
		return MakeAssetContentVersion(
			ToAssetKind(identity.m_Kind), identity.m_StableId, identity.m_Generation);
	}

	[[nodiscard]] constexpr AssetStreamingIdentity ToAssetStreamingIdentity(
		const AssetContentVersion& contentVersion) noexcept
	{
		return {
			.m_Kind = ToAssetStreamingWorkKind(contentVersion.m_Key.m_Kind),
			.m_StableId = contentVersion.m_Key.m_StableId,
			.m_Generation = contentVersion.m_ContentGeneration,
		};
	}

	struct AssetResourcePublicationStageStatistics
	{
		uint64_t m_StepCount = 0;
		double m_TotalMilliseconds = 0.0;
		double m_MaxMilliseconds = 0.0;
		double m_P95Milliseconds = 0.0;
	};

	struct AssetStreamingWorkEstimate
	{
		uint64_t m_SourceBytes = 0;
		uint64_t m_StagingBytes = 0;
		uint32_t m_OperationCount = 0;
	};

	struct AssetStreamingFrameBudget
	{
		uint32_t m_MaxCpuPayloadItems = 8;
		double m_MaxCpuPayloadMilliseconds = 0.5;
		uint32_t m_MaxResourcePublicationSteps = 16;
		uint32_t m_MaxResourcePublicationCreations = 8;
		double m_MaxResourcePublicationMilliseconds = 0.5;
		uint32_t m_MaxUploadRecordingItems = 8;
		uint64_t m_MaxUploadBytes = 32ull * 1024ull * 1024ull;
		uint32_t m_MaxUploadOperations = 64;
		double m_MaxUploadRecordingMilliseconds = 1.0;
		uint32_t m_MaxGpuFinalizeItems = 16;
		double m_MaxGpuFinalizeMilliseconds = 0.5;
		uint64_t m_MaxInFlightBytes = 256ull * 1024ull * 1024ull;
		uint64_t m_MaxUploadRecordingBacklogBytes = 512ull * 1024ull * 1024ull;
	};

	struct AssetStreamingFrameUsage
	{
		uint32_t m_CpuPayloadItems = 0;
		double m_CpuPayloadMilliseconds = 0.0;
		uint32_t m_ResourcePublicationSteps = 0;
		uint32_t m_ResourcePublicationCreations = 0;
		double m_ResourcePublicationMilliseconds = 0.0;
		uint32_t m_UploadRecordingItems = 0;
		uint64_t m_UploadBytes = 0;
		uint32_t m_UploadOperations = 0;
		double m_UploadRecordingMilliseconds = 0.0;
		uint32_t m_GpuFinalizeItems = 0;
		double m_GpuFinalizeMilliseconds = 0.0;
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
		uint64_t m_ContinueCount = 0;
		uint64_t m_CompletedCount = 0;
		uint64_t m_FailedCount = 0;
		uint64_t m_CallbackFailureCount = 0;
		uint64_t m_CancelledCount = 0;
		uint64_t m_ResourceCreationCount = 0;
		uint64_t m_SourceBytesReleased = 0;
		uint64_t m_SourceBytesCopiedToUpload = 0;
		uint64_t m_QueueSampleCount = 0;
		uint64_t m_PendingSourceBytes = 0;
		uint64_t m_PendingStagingBytes = 0;
		uint64_t m_PendingOperationCount = 0;
		double m_TotalQueueMilliseconds = 0.0;
		double m_MaxQueueMilliseconds = 0.0;
		double m_TotalExecutionMilliseconds = 0.0;
		double m_MaxExecutionMilliseconds = 0.0;
		double m_ExecutionP95Milliseconds = 0.0;
		uint64_t m_OverBudgetExecutionCount = 0;
		uint64_t m_NoProgressContinueCount = 0;
		uint64_t m_FaultInjectionCount = 0;
		std::array<AssetResourcePublicationStageStatistics,
			static_cast<size_t>(AssetResourcePublicationStage::Count)>
			m_PublicationStages;
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
	using AssetUploadRecord = std::function<bool(TransferBatch&)>;

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
		AssetStreamingQueueStatistics m_CpuPayloadQueue;
		AssetStreamingQueueStatistics m_ResourcePublicationQueue;
		AssetStreamingQueueStatistics m_UploadRecordingQueue;
		AssetStreamingQueueStatistics m_GpuFinalizeQueue;
		AssetStreamingFrameBudget m_FrameBudget;
		AssetStreamingFrameUsage m_LastFrameUsage;
		uint64_t m_ReadyPayloadBytes = 0;
		uint64_t m_ReadyPayloadHighWatermark = 0;
		uint64_t m_InFlightBytes = 0;
		uint64_t m_InFlightHighWatermark = 0;
		uint64_t m_UploadPromotionBudgetDeferralCount = 0;
		uint64_t m_UploadBudgetDeferralCount = 0;
		uint64_t m_InFlightBudgetDeferralCount = 0;
		uint64_t m_OversizedAdmissionCount = 0;
		uint32_t m_PendingCount = 0;
		uint64_t m_BatchSubmissionCount = 0;
		uint32_t m_LastBatchUploadCount = 0;
		uint32_t m_MaxUploadsPerBatch = 0;
		uint64_t m_SubmittedCount = 0;
		uint64_t m_SucceededCount = 0;
		uint64_t m_FailedCount = 0;
		uint64_t m_CompletionCallbackFailureCount = 0;
		std::vector<AssetUploadActivity> m_PendingUploads;
		std::vector<AssetUploadActivity> m_RecentUploads;
	};

	enum class AssetResourcePublicationFaultAction : uint8_t
	{
		None,
		Fail,
		Cancel,
	};

	enum class AssetResourcePublicationFaultTiming : uint8_t
	{
		AfterStep,
		BeforeStep,
	};

	struct AssetResourcePublicationFaultInjection
	{
		AssetStreamingIdentity m_Identity{};
		AssetResourcePublicationStage m_Stage = AssetResourcePublicationStage::Unknown;
		AssetResourcePublicationFaultAction m_Action = AssetResourcePublicationFaultAction::None;
		AssetResourcePublicationFaultTiming m_Timing =
			AssetResourcePublicationFaultTiming::AfterStep;
		uint32_t m_TriggerOccurrence = 1;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Identity.m_Kind != AssetStreamingWorkKind::Unknown &&
				m_Stage != AssetResourcePublicationStage::Unknown &&
				m_Stage != AssetResourcePublicationStage::Count &&
				m_Action != AssetResourcePublicationFaultAction::None && m_TriggerOccurrence > 0;
		}
	};

	// Owns the main-thread streaming boundaries around resource publication and
	// transfer-queue work. Jobs advance only at explicit publication step boundaries.
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

		void EnqueueCpuPayload(AssetStreamingWorkDesc desc, AssetStreamingWork work) noexcept;
		void EnqueueResourcePublication(
			AssetStreamingWorkDesc desc, std::unique_ptr<IResourcePublicationJob>&& job) noexcept;
		void EnqueueUploadRecording(AssetStreamingWorkDesc desc, AssetStreamingWork work) noexcept;
		uint32_t CancelReadyWork(const AssetContentVersion& contentVersion) noexcept;
		uint32_t CancelReadyWork(const AssetStreamingIdentity& identity) noexcept;
		uint32_t UpdateWorkPriority(
			const AssetContentVersion& contentVersion, TaskPriority priority) noexcept;
		uint32_t UpdateWorkPriority(
			const AssetStreamingIdentity& identity, TaskPriority priority) noexcept;

		[[nodiscard]] AssetUploadHandle RecordUpload(AssetUploadDesc desc, AssetUploadRecord record,
			AssetUploadCompletion completion = {}) noexcept;
		uint32_t Tick() noexcept;
		void DrainReadyWork() noexcept;
		void Finalize() noexcept;
		void SetFrameBudget(const AssetStreamingFrameBudget& budget) noexcept;
		[[nodiscard]] const AssetStreamingFrameBudget& GetFrameBudget() const noexcept
		{
			return m_FrameBudget;
		}
		void ArmResourcePublicationFault(
			const AssetResourcePublicationFaultInjection& fault) noexcept;
		void ClearResourcePublicationFault() noexcept;
		void ArmGpuCompletionHold(const AssetStreamingIdentity& identity) noexcept;
		void ClearGpuCompletionHold() noexcept;

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

		struct QueuedResourcePublication
		{
			AssetStreamingWorkDesc m_Desc;
			std::chrono::steady_clock::time_point m_QueuedAt{};
			AssetResourcePublicationPayloadState m_Payload{};
			bool m_HasStarted = false;
			std::unique_ptr<IResourcePublicationJob> m_Job;
		};

		struct PendingGpuFinalize
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
			uint64_t m_ContinueCount = 0;
			uint64_t m_CompletedCount = 0;
			uint64_t m_FailedCount = 0;
			uint64_t m_CallbackFailureCount = 0;
			uint64_t m_CancelledCount = 0;
			uint64_t m_ResourceCreationCount = 0;
			uint64_t m_SourceBytesReleased = 0;
			uint64_t m_SourceBytesCopiedToUpload = 0;
			uint64_t m_QueueSampleCount = 0;
			double m_TotalQueueMilliseconds = 0.0;
			double m_MaxQueueMilliseconds = 0.0;
			double m_TotalExecutionMilliseconds = 0.0;
			double m_MaxExecutionMilliseconds = 0.0;
			std::deque<double> m_RecentExecutionMilliseconds;
		};

		struct PublicationStageTelemetry
		{
			uint64_t m_StepCount = 0;
			double m_TotalMilliseconds = 0.0;
			double m_MaxMilliseconds = 0.0;
			std::deque<double> m_RecentExecutionMilliseconds;
		};

		[[nodiscard]] bool IsOwnerThread() const noexcept;
		void EnqueueWork(std::deque<QueuedWork>& queue, QueueTelemetry& telemetry,
			AssetStreamingWorkDesc desc, AssetStreamingWork work) noexcept;
		[[nodiscard]] double ExecuteWork(
			QueuedWork&& queued, QueueTelemetry& telemetry, std::string_view queueName) noexcept;
		void InsertResourcePublication(QueuedResourcePublication&& publication) noexcept;
		uint32_t DrainCpuPayloadQueue(bool ignoreBudget) noexcept;
		uint32_t DrainResourcePublicationQueue(bool ignoreBudget) noexcept;
		[[nodiscard]] bool TryApplyResourcePublicationFault(const AssetStreamingIdentity& identity,
			AssetResourcePublicationStage stage, AssetResourcePublicationFaultTiming timing,
			AssetResourcePublicationStepResult& result) noexcept;
		uint32_t DrainUploadRecordingQueue(bool ignoreBudget) noexcept;
		void FlushRecordedUploads() noexcept;
		uint32_t PollCompletedUploads() noexcept;
		void EnqueueGpuFinalize(PendingUpload&& upload, AssetUploadStatus status) noexcept;
		uint32_t DrainGpuFinalizeQueue(bool ignoreBudget) noexcept;
		void RemoveReadyPayload(
			const AssetStreamingWorkEstimate& estimate, bool uploadRecording) noexcept;
		[[nodiscard]] uint64_t RetireResourcePublicationPayload(
			QueuedResourcePublication& publication,
			const AssetResourcePublicationStepUsage& usage) noexcept;
		void RetireTerminalResourcePublicationPayload(
			QueuedResourcePublication& publication) noexcept;
		void RetireReadyPayloadBytes(uint64_t bytes) noexcept;
		uint32_t CancelQueuedWork(std::deque<QueuedWork>& queue, QueueTelemetry& telemetry,
			const AssetStreamingIdentity& identity, bool uploadRecording) noexcept;
		uint32_t CancelResourcePublication(const AssetStreamingIdentity& identity,
			AssetResourcePublicationAbortReason reason) noexcept;
		uint32_t UpdateQueuedWorkPriority(std::deque<QueuedWork>& queue,
			const AssetStreamingIdentity& identity, TaskPriority priority) noexcept;
		uint32_t UpdateResourcePublicationPriority(
			const AssetStreamingIdentity& identity, TaskPriority priority) noexcept;
		[[nodiscard]] AssetStreamingQueueStatistics BuildQueueStatistics(
			const std::deque<QueuedWork>& queue, const QueueTelemetry& telemetry) const;
		[[nodiscard]] AssetStreamingQueueStatistics BuildResourcePublicationQueueStatistics() const;
		[[nodiscard]] AssetStreamingQueueStatistics BuildGpuFinalizeQueueStatistics() const;
		void FinishUpload(PendingUpload&& upload, AssetUploadStatus status) noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		TransferManager* m_TransferManager = nullptr;
		std::thread::id m_OwnerThreadId;
		uint32_t m_RecentUploadCapacity = 0;
		AssetStreamingFrameBudget m_FrameBudget{};
		AssetStreamingFrameUsage m_LastFrameUsage{};
		uint64_t m_NextHandle = 1;
		uint64_t m_ReadyPayloadBytes = 0;
		uint64_t m_UploadRecordingBacklogBytes = 0;
		uint64_t m_ReadyPayloadHighWatermark = 0;
		uint64_t m_InFlightBytes = 0;
		uint64_t m_RecordedUploadBytes = 0;
		uint64_t m_InFlightHighWatermark = 0;
		uint64_t m_UploadPromotionBudgetDeferralCount = 0;
		uint64_t m_UploadBudgetDeferralCount = 0;
		uint64_t m_InFlightBudgetDeferralCount = 0;
		uint64_t m_OversizedAdmissionCount = 0;
		uint64_t m_BatchSubmissionCount = 0;
		uint32_t m_LastBatchUploadCount = 0;
		uint32_t m_MaxUploadsPerBatch = 0;
		uint64_t m_SubmittedCount = 0;
		uint64_t m_SucceededCount = 0;
		uint64_t m_FailedCount = 0;
		uint64_t m_CompletionCallbackFailureCount = 0;
		std::deque<QueuedWork> m_CpuPayloadQueue;
		std::deque<QueuedResourcePublication> m_ResourcePublicationQueue;
		std::deque<QueuedWork> m_UploadRecordingQueue;
		std::unique_ptr<TransferBatch> m_RecordingBatch;
		std::vector<PendingUpload> m_RecordedUploads;
		std::deque<PendingUpload> m_PendingUploads;
		std::deque<PendingGpuFinalize> m_GpuFinalizeQueue;
		std::deque<AssetUploadActivity> m_RecentUploads;
		QueueTelemetry m_CpuPayloadTelemetry;
		QueueTelemetry m_ResourcePublicationTelemetry;
		QueueTelemetry m_UploadRecordingTelemetry;
		QueueTelemetry m_GpuFinalizeTelemetry;
		std::array<PublicationStageTelemetry,
			static_cast<size_t>(AssetResourcePublicationStage::Count)>
			m_PublicationStageTelemetry;
		AssetResourcePublicationFaultInjection m_PublicationFault{};
		uint32_t m_PublicationFaultObservedOccurrences = 0;
		AssetStreamingIdentity m_GpuCompletionHold{};
		uint64_t m_PublicationOverBudgetExecutionCount = 0;
		uint64_t m_PublicationNoProgressContinueCount = 0;
		uint64_t m_PublicationFaultInjectionCount = 0;
		bool m_IsRecordingUploadBatch = false;
	};
}
