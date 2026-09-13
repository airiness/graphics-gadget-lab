#pragma once
#include "GGLabFoundation/Async/ProgressChannel.h"
#include "GGLabFoundation/Task/TaskTypes.h"
#include "GGLabRuntime/Graphics/Asset/AssetResourcePublicationTypes.h"
#include "GGLabRuntime/Graphics/Asset/AssetStreamingTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gglab
{
	// Immutable upload-work descriptions and snapshots shared by the asset
	// upload scheduler and its explicit developer-control contract.
	using AssetStreamingWork = std::function<void()>;

	struct AssetStreamingWorkDesc
	{
		std::string m_Name;
		AssetStreamingIdentity m_Identity{};
		AssetStreamingWorkEstimate m_Estimate{};
		TaskPriority m_Priority = TaskPriority::Normal;
		ProgressChannelPtr m_Progress;
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

	class TransferBatch;
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
}
