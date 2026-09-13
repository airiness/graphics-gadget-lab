#pragma once
#include "GGLabFoundation/Async/ProgressChannel.h"
#include "GGLabFoundation/Task/TaskTypes.h"
#include "GGLabRuntime/Graphics/Asset/AssetResourcePublicationTypes.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gglab
{
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
		RuntimeMesh,
	};

	struct AssetStreamingIdentity
	{
		AssetStreamingWorkKind m_Kind = AssetStreamingWorkKind::Unknown;
		uint64_t m_StableId = 0;
		uint64_t m_Generation = 0;

		friend constexpr bool operator==(
			const AssetStreamingIdentity&, const AssetStreamingIdentity&) = default;
	};

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
}
