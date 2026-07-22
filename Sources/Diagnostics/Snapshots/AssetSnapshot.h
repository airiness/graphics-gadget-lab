#pragma once
#include "Diagnostics/SnapshotCommon.h"
#include "Graphics/Asset/Streaming/AssetUploadScheduler.h"
#include "Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "Graphics/GraphicsTypes.h"

namespace gglab
{
	struct AssetSnapshot
	{
		struct Model
		{
			ModelID m_Id{};
			uint64_t m_ContentGeneration = 0;
			uint64_t m_ResidencyEpoch = 0;
			uint64_t m_LastUsedFrame = 0;
			uint64_t m_UseCount = 0;
			AssetState m_State = AssetState::Unloaded;
			AssetContentState m_ContentState = AssetContentState::Unloaded;
			AssetResidencyState m_ResidencyState = AssetResidencyState::NonResident;
			AssetResidencyPolicy m_ResidencyPolicy = AssetResidencyPolicy::Cacheable;
			std::filesystem::path m_SourcePath;
			ModelType m_Type = ModelType::Invalid;
			StringID m_Name{};
			ArtifactContentDigest m_ImportArtifactContentDigest{};
			uint32_t m_MeshInstanceCount = 0;
			uint32_t m_DependencyCount = 0;
			uint32_t m_ReadyDependencyCount = 0;
			uint32_t m_PendingDependencyCount = 0;
			uint32_t m_FailedDependencyCount = 0;
			uint32_t m_CancelledDependencyCount = 0;
			uint64_t m_DependencyEventUpdateCount = 0;
			bool m_HasDependencyState = false;
			bool m_IsImportArtifactCached = false;
			bool m_IsEvictionCandidate = false;
		};

		struct Mesh
		{
			MeshID m_Id{};
			uint64_t m_ContentGeneration = 0;
			uint64_t m_ResidencyEpoch = 0;
			uint64_t m_LastUsedFrame = 0;
			uint64_t m_UseCount = 0;
			AssetState m_State = AssetState::Unloaded;
			AssetContentState m_ContentState = AssetContentState::Unloaded;
			AssetResidencyState m_ResidencyState = AssetResidencyState::NonResident;
			AssetResidencyPolicy m_ResidencyPolicy = AssetResidencyPolicy::Cacheable;
			StringID m_Name{};
			uint32_t m_VertexCount = 0;
			uint32_t m_IndexCount = 0;
			bool m_IsUploaded = false;
			bool m_IsEvictionCandidate = false;
		};

		struct Texture
		{
			TextureID m_Id{};
			uint64_t m_ContentGeneration = 0;
			uint64_t m_ResidencyEpoch = 0;
			uint64_t m_ResidencyOperationSerial = 0;
			uint64_t m_LastUsedFrame = 0;
			uint64_t m_UseCount = 0;
			AssetState m_State = AssetState::Unloaded;
			AssetContentState m_ContentState = AssetContentState::Unloaded;
			AssetResidencyState m_ResidencyState = AssetResidencyState::NonResident;
			AssetResidencyPolicy m_ResidencyPolicy = AssetResidencyPolicy::Cacheable;
			std::filesystem::path m_SourcePath;
			TextureImportSettings m_ImportSettings{};
			TextureSemantic m_Semantic = TextureSemantic::Unknown;
			StringID m_Name{};
			RHITextureHandle m_Texture{};
			std::string m_DebugName;
			ArtifactContentDigest m_ArtifactContentDigest{};
			SourceDigest m_SourceDigest{};
			DerivedDataKey m_DerivedDataKey{};
			bool m_IsUploaded = false;
			bool m_HasSrv = false;
			bool m_IsReserved = false;
			bool m_IsCpuArtifactCached = false;
			bool m_IsDerivedDataCached = false;
			bool m_IsEvictionCandidate = false;
		};

		struct Upload
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

		struct OwnershipInterest
		{
			AssetStreamingWorkKind m_Kind = AssetStreamingWorkKind::Model;
			uint64_t m_StableId = 0;
			uint64_t m_Generation = 0;
			uint32_t m_LeaseCount = 0;
			uint32_t m_OwnerCount = 0;
			TaskPriority m_EffectivePriority = TaskPriority::Normal;
		};

		std::vector<Model> m_Models;
		std::vector<Mesh> m_Meshes;
		std::vector<Texture> m_Textures;
		uint64_t m_AssetUsageFrame = 0;
		uint32_t m_ResidentAssetCount = 0;
		uint32_t m_PinnedAssetCount = 0;
		uint32_t m_CacheableAssetCount = 0;
		uint32_t m_EvictionCandidateCount = 0;
		uint32_t m_TrackedModelDependencyCount = 0;
		uint32_t m_ReverseDependencyCount = 0;
		uint32_t m_ReverseDependencyEdgeCount = 0;
		uint64_t m_DependencyGraphBuildCount = 0;
		uint64_t m_DependencyEventUpdateCount = 0;
		uint64_t m_DependencyValidationCount = 0;
		uint64_t m_DependencyValidationMismatchCount = 0;
		bool m_AutomaticResidencyEvictionEnabled = false;
		uint64_t m_ResidencyHighWatermarkBytes = 0;
		uint64_t m_ResidencyLowWatermarkBytes = 0;
		uint64_t m_ResidencyMinUnusedFrames = 0;
		uint32_t m_MaxResidencyEvictionsPerFrame = 0;
		uint64_t m_LogicalResidentBytes = 0;
		uint64_t m_PendingEvictionBytes = 0;
		uint32_t m_PendingEvictionCount = 0;
		uint32_t m_ReloadingAssetCount = 0;
		uint64_t m_ResidencyEvictionCount = 0;
		uint64_t m_ResidencyEvictedBytes = 0;
		uint64_t m_ResidencyEvictionCancellationCount = 0;
		uint64_t m_ResidencyReloadRequestCount = 0;
		uint64_t m_ResidencyReloadCoalescedCount = 0;
		uint32_t m_LastFrameReloadRequestCount = 0;
		uint32_t m_ReloadRequestHighWatermark = 0;
		uint64_t m_ResidencyPlanningCount = 0;
		uint64_t m_LastResidencyPlanFrame = 0;
		uint32_t m_LastPlannedResidencyActionCount = 0;
		uint64_t m_LastPlannedResidencyBytes = 0;
		uint64_t m_ResidencyOperationCount = 0;
		uint64_t m_ResidencyAcceptedStateEventCount = 0;
		uint64_t m_ResidencyCompletedStateEventCount = 0;
		uint64_t m_ResidencyStaleStateEventCount = 0;
		uint64_t m_ResidencyRevalidationRejectionCount = 0;
		uint64_t m_ResidencyStaleCompletionCount = 0;
		uint64_t m_ModelImportArtifactCacheBudgetBytes = 0;
		uint64_t m_ModelImportArtifactCachedBytes = 0;
		uint64_t m_ModelImportArtifactExternallyRetainedBytes = 0;
		uint64_t m_ModelImportArtifactTotalLiveBytes = 0;
		uint32_t m_ModelImportArtifactCachedEntryCount = 0;
		uint64_t m_ModelImportArtifactCacheHitCount = 0;
		uint64_t m_ModelImportArtifactCacheMissCount = 0;
		uint64_t m_ModelImportArtifactAdmissionCount = 0;
		uint64_t m_ModelImportArtifactAdmissionRejectedCount = 0;
		uint64_t m_ModelImportArtifactEvictionCount = 0;
		uint64_t m_ModelImportArtifactEvictedBytes = 0;
		uint64_t m_TextureArtifactCacheBudgetBytes = 0;
		uint64_t m_TextureArtifactCachedBytes = 0;
		uint64_t m_TextureArtifactExternallyRetainedBytes = 0;
		uint64_t m_TextureArtifactTotalLiveBytes = 0;
		uint32_t m_TextureArtifactCachedEntryCount = 0;
		uint64_t m_TextureArtifactCacheHitCount = 0;
		uint64_t m_TextureArtifactCacheMissCount = 0;
		uint64_t m_TextureArtifactAdmissionCount = 0;
		uint64_t m_TextureArtifactAdmissionRejectedCount = 0;
		uint64_t m_TextureArtifactEvictionCount = 0;
		uint64_t m_TextureArtifactEvictedBytes = 0;
		uint64_t m_TextureDerivedDataStoredBytes = 0;
		uint64_t m_TextureDerivedDataStoredEntryCount = 0;
		uint64_t m_TextureDerivedDataHitCount = 0;
		uint64_t m_TextureDerivedDataMissCount = 0;
		uint64_t m_TextureDerivedDataCorruptionCount = 0;
		uint64_t m_TextureDerivedDataReadBytes = 0;
		uint64_t m_TextureDerivedDataWriteCount = 0;
		uint64_t m_TextureDerivedDataWriteFailureCount = 0;
		uint64_t m_TextureDerivedDataWrittenBytes = 0;
		uint64_t m_TextureDerivedDataCatalogLastReconciledAtUnixMilliseconds = 0;
		uint64_t m_TextureDerivedDataCatalogReconciliationCount = 0;
		uint64_t m_TextureDerivedDataCatalogReconciliationFailureCount = 0;
		bool m_IsTextureDerivedDataCatalogApproximate = true;
		uint32_t m_TextureDerivedDataActiveBuildCount = 0;
		uint32_t m_TextureDerivedDataActiveWaiterCount = 0;
		uint64_t m_TextureDerivedDataRequestCount = 0;
		uint64_t m_TextureDerivedDataImmediateHitCount = 0;
		uint64_t m_TextureDerivedDataWaitCount = 0;
		uint64_t m_TextureDerivedDataBuildRequiredCount = 0;
		uint64_t m_TextureDerivedDataPublishCount = 0;
		uint64_t m_TextureDerivedDataBuildFailureCount = 0;
		uint64_t m_TextureDerivedDataCancelledWaiterCount = 0;
		uint64_t m_TextureDerivedDataFanoutDeliveryCount = 0;
		AssetStreamingQueueStatistics m_CpuPayloadQueue;
		AssetStreamingQueueStatistics m_ResourcePublicationQueue;
		AssetStreamingQueueStatistics m_UploadRecordingQueue;
		AssetStreamingQueueStatistics m_GpuFinalizeQueue;
		AssetStreamingFrameBudget m_StreamingFrameBudget;
		AssetStreamingFrameUsage m_LastStreamingFrameUsage;
		uint64_t m_ReadyPayloadBytes = 0;
		uint64_t m_ReadyPayloadHighWatermark = 0;
		uint64_t m_InFlightUploadBytes = 0;
		uint64_t m_InFlightUploadHighWatermark = 0;
		uint64_t m_UploadPromotionBudgetDeferralCount = 0;
		uint64_t m_UploadBudgetDeferralCount = 0;
		uint64_t m_InFlightBudgetDeferralCount = 0;
		uint64_t m_OversizedAdmissionCount = 0;
		uint32_t m_PendingUploadCount = 0;
		uint64_t m_UploadBatchSubmissionCount = 0;
		uint32_t m_LastUploadBatchResourceCount = 0;
		uint32_t m_MaxResourcesPerUploadBatch = 0;
		uint64_t m_SubmittedUploadCount = 0;
		uint64_t m_SucceededUploadCount = 0;
		uint64_t m_FailedUploadCount = 0;
		uint64_t m_UploadCompletionCallbackFailureCount = 0;
		uint32_t m_AssetOwnerCount = 0;
		uint32_t m_AssetLeaseCount = 0;
		uint32_t m_ManagedAssetCount = 0;
		uint64_t m_OwnershipPriorityUpdateCount = 0;
		uint64_t m_OwnershipCpuCancellationCount = 0;
		uint64_t m_OwnershipReadyCancellationCount = 0;
		uint64_t m_OwnershipGpuDeferredCancellationCount = 0;
		uint64_t m_OwnershipReadyRetentionCount = 0;
		uint64_t m_PublicationRetainCount = 0;
		uint64_t m_PublicationProtectedCancellationCount = 0;
		std::vector<OwnershipInterest> m_ActiveOwnershipInterests;
		std::vector<Upload> m_PendingUploads;
		std::vector<Upload> m_RecentUploads;
	};

	template<>
	struct SnapshotTraits<AssetSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.AssetSnapshot");
	};
}
