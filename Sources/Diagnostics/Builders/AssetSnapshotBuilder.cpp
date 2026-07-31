#include "Core/Precompiled.h"
#include "Diagnostics/Builders/AssetSnapshotBuilder.h"
#include "Diagnostics/Snapshots/AssetSnapshot.h"
#include "Graphics/Asset/AssetIdentityConversions.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/Asset/Streaming/AssetUploadScheduler.h"

#include <algorithm>

namespace gglab
{
	AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept
	{
		AssetSnapshot snapshot{};
		snapshot.m_AssetUsageFrame = assetManager.m_AssetUsageFrame;
		const AssetDependencyGraphStatistics dependencyStatistics =
			assetManager.m_AssetDependencyGraph.GetStatistics();
		snapshot.m_TrackedModelDependencyCount = dependencyStatistics.m_TrackedModelCount;
		snapshot.m_ReverseDependencyCount = dependencyStatistics.m_ReverseDependencyCount;
		snapshot.m_ReverseDependencyEdgeCount = dependencyStatistics.m_ReverseDependencyEdgeCount;
		snapshot.m_DependencyGraphBuildCount = dependencyStatistics.m_GraphBuildCount;
		snapshot.m_DependencyEventUpdateCount = dependencyStatistics.m_EventUpdateCount;
		snapshot.m_DependencyValidationCount = assetManager.m_DependencyValidationCount;
		snapshot.m_DependencyValidationMismatchCount =
			assetManager.m_DependencyValidationMismatchCount;
		const AssetResidencyStatistics residency = assetManager.GetResidencyStatistics();
		snapshot.m_AutomaticResidencyEvictionEnabled = residency.m_Config.m_EnableAutomaticEviction;
		snapshot.m_ResidencyHighWatermarkBytes = residency.m_Config.m_HighWatermarkBytes;
		snapshot.m_ResidencyLowWatermarkBytes = residency.m_Config.m_LowWatermarkBytes;
		snapshot.m_ResidencyMinUnusedFrames = residency.m_Config.m_MinUnusedFrames;
		snapshot.m_MaxResidencyEvictionsPerFrame = residency.m_Config.m_MaxEvictionsPerFrame;
		snapshot.m_RuntimeEntryRetentionFrames = residency.m_Config.m_RuntimeEntryRetentionFrames;
		snapshot.m_MaxRuntimeRetirementsPerFrame =
			residency.m_Config.m_MaxRuntimeRetirementsPerFrame;
		snapshot.m_LogicalResidentBytes = residency.m_LogicalResidentBytes;
		snapshot.m_PendingEvictionBytes = residency.m_PendingEvictionBytes;
		snapshot.m_PendingEvictionCount = residency.m_PendingEvictionCount;
		snapshot.m_ReloadingAssetCount = residency.m_ReloadingAssetCount;
		snapshot.m_ResidencyEvictionCount = residency.m_EvictionCount;
		snapshot.m_ResidencyEvictedBytes = residency.m_EvictedBytes;
		snapshot.m_ResidencyEvictionCancellationCount = residency.m_EvictionCancellationCount;
		snapshot.m_ResidencyReloadRequestCount = residency.m_ReloadRequestCount;
		snapshot.m_ResidencyReloadCoalescedCount = residency.m_ReloadCoalescedCount;
		snapshot.m_LastFrameReloadRequestCount = residency.m_LastFrameReloadRequestCount;
		snapshot.m_ReloadRequestHighWatermark = residency.m_ReloadRequestHighWatermark;
		snapshot.m_ResidencyPlanningCount = residency.m_PlanningCount;
		snapshot.m_LastResidencyPlanFrame = residency.m_LastPlanFrame;
		snapshot.m_LastPlannedResidencyActionCount = residency.m_LastPlannedActionCount;
		snapshot.m_LastPlannedResidencyBytes = residency.m_LastPlannedBytes;
		snapshot.m_ResidencyOperationCount = residency.m_OperationCount;
		snapshot.m_ResidencyAcceptedStateEventCount = residency.m_AcceptedStateEventCount;
		snapshot.m_ResidencyCompletedStateEventCount = residency.m_CompletedStateEventCount;
		snapshot.m_ResidencyStaleStateEventCount = residency.m_StaleStateEventCount;
		snapshot.m_ResidencyRevalidationRejectionCount = residency.m_RevalidationRejectionCount;
		snapshot.m_ResidencyStaleCompletionCount = residency.m_StaleCompletionCount;
		const ModelImportArtifactCacheStatistics modelArtifactCache =
			assetManager.GetModelImportArtifactCacheStatistics();
		snapshot.m_ModelImportArtifactCacheBudgetBytes = modelArtifactCache.m_BudgetBytes;
		snapshot.m_ModelImportArtifactCachedBytes = modelArtifactCache.m_CachedBytes;
		snapshot.m_ModelImportArtifactExternallyRetainedBytes =
			modelArtifactCache.m_ExternallyRetainedBytes;
		snapshot.m_ModelImportArtifactTotalLiveBytes = modelArtifactCache.m_TotalLiveBytes;
		snapshot.m_ModelImportArtifactCachedEntryCount = modelArtifactCache.m_CachedEntryCount;
		snapshot.m_ModelImportArtifactCacheHitCount = modelArtifactCache.m_HitCount;
		snapshot.m_ModelImportArtifactCacheMissCount = modelArtifactCache.m_MissCount;
		snapshot.m_ModelImportArtifactAdmissionCount = modelArtifactCache.m_AdmissionCount;
		snapshot.m_ModelImportArtifactAdmissionRejectedCount =
			modelArtifactCache.m_AdmissionRejectedCount;
		snapshot.m_ModelImportArtifactEvictionCount = modelArtifactCache.m_EvictionCount;
		snapshot.m_ModelImportArtifactEvictedBytes = modelArtifactCache.m_EvictedBytes;
		const TextureArtifactCacheStatistics artifactCache =
			assetManager.GetTextureArtifactCacheStatistics();
		snapshot.m_TextureArtifactCacheBudgetBytes = artifactCache.m_BudgetBytes;
		snapshot.m_TextureArtifactCachedBytes = artifactCache.m_CachedBytes;
		snapshot.m_TextureArtifactExternallyRetainedBytes = artifactCache.m_ExternallyRetainedBytes;
		snapshot.m_TextureArtifactTotalLiveBytes = artifactCache.m_TotalLiveBytes;
		snapshot.m_TextureArtifactCachedEntryCount = artifactCache.m_CachedEntryCount;
		snapshot.m_TextureArtifactCacheHitCount = artifactCache.m_HitCount;
		snapshot.m_TextureArtifactCacheMissCount = artifactCache.m_MissCount;
		snapshot.m_TextureArtifactAdmissionCount = artifactCache.m_AdmissionCount;
		snapshot.m_TextureArtifactAdmissionRejectedCount = artifactCache.m_AdmissionRejectedCount;
		snapshot.m_TextureArtifactEvictionCount = artifactCache.m_EvictionCount;
		snapshot.m_TextureArtifactEvictedBytes = artifactCache.m_EvictedBytes;
		const LocalDerivedDataStoreStatistics derivedData =
			assetManager.GetTextureDerivedDataStatistics();
		snapshot.m_TextureDerivedDataStoredBytes = derivedData.m_StoredBytes;
		snapshot.m_TextureDerivedDataStoredEntryCount = derivedData.m_StoredEntryCount;
		snapshot.m_TextureDerivedDataHitCount = derivedData.m_HitCount;
		snapshot.m_TextureDerivedDataMissCount = derivedData.m_MissCount;
		snapshot.m_TextureDerivedDataCorruptionCount = derivedData.m_CorruptionCount;
		snapshot.m_TextureDerivedDataReadBytes = derivedData.m_ReadBytes;
		snapshot.m_TextureDerivedDataWriteCount = derivedData.m_WriteCount;
		snapshot.m_TextureDerivedDataWriteFailureCount = derivedData.m_WriteFailureCount;
		snapshot.m_TextureDerivedDataWrittenBytes = derivedData.m_WrittenBytes;
		snapshot.m_TextureDerivedDataCatalogLastReconciledAtUnixMilliseconds =
			derivedData.m_CatalogLastReconciledAtUnixMilliseconds;
		snapshot.m_TextureDerivedDataCatalogReconciliationCount =
			derivedData.m_CatalogReconciliationCount;
		snapshot.m_TextureDerivedDataCatalogReconciliationFailureCount =
			derivedData.m_CatalogReconciliationFailureCount;
		snapshot.m_IsTextureDerivedDataCatalogApproximate = derivedData.m_IsCatalogApproximate;
		const TextureDerivedDataCoordinatorStatistics derivedDataCoordinator =
			assetManager.GetTextureDerivedDataCoordinatorStatistics();
		snapshot.m_TextureDerivedDataActiveBuildCount = derivedDataCoordinator.m_ActiveBuildCount;
		snapshot.m_TextureDerivedDataActiveWaiterCount = derivedDataCoordinator.m_ActiveWaiterCount;
		snapshot.m_TextureDerivedDataRequestCount = derivedDataCoordinator.m_RequestCount;
		snapshot.m_TextureDerivedDataImmediateHitCount = derivedDataCoordinator.m_ImmediateHitCount;
		snapshot.m_TextureDerivedDataWaitCount = derivedDataCoordinator.m_WaitCount;
		snapshot.m_TextureDerivedDataBuildRequiredCount =
			derivedDataCoordinator.m_BuildRequiredCount;
		snapshot.m_TextureDerivedDataPublishCount = derivedDataCoordinator.m_PublishCount;
		snapshot.m_TextureDerivedDataBuildFailureCount = derivedDataCoordinator.m_BuildFailureCount;
		snapshot.m_TextureDerivedDataCancelledWaiterCount =
			derivedDataCoordinator.m_CancelledWaiterCount;
		snapshot.m_TextureDerivedDataFanoutDeliveryCount =
			derivedDataCoordinator.m_FanoutDeliveryCount;

		const auto isEvictionCandidate = [&assetManager](AssetKind kind, uint64_t stableId,
			const AssetLifecycle& lifecycle) noexcept
			{
				const AssetKey key = MakeAssetKey(kind, stableId);
				return lifecycle.m_ResidencyPolicy == AssetResidencyPolicy::Cacheable &&
					lifecycle.m_ResidencyState == AssetResidencyState::Resident &&
					!assetManager.HasActiveInterest(key) &&
					!assetManager.HasPublicationRetain(key, lifecycle.m_ContentGeneration);
			};
		const auto recordResidency =
			[&snapshot](const AssetLifecycle& lifecycle, bool evictionCandidate) noexcept
			{
				GGLAB_ASSERT_MSG(IsAssetLifecycleSynchronized(lifecycle),
					"Asset aggregate, content, and residency states diverged.");
				snapshot.m_ResidentAssetCount +=
					lifecycle.m_ResidencyState == AssetResidencyState::Resident ? 1u : 0u;
				snapshot.m_PinnedAssetCount +=
					lifecycle.m_ResidencyPolicy == AssetResidencyPolicy::Pinned ? 1u : 0u;
				snapshot.m_CacheableAssetCount +=
					lifecycle.m_ResidencyPolicy == AssetResidencyPolicy::Cacheable ? 1u : 0u;
				snapshot.m_EvictionCandidateCount += evictionCandidate ? 1u : 0u;
			};

		const ModelStore::EntryMap& models = assetManager.m_ModelStore.Entries();
		snapshot.m_Models.reserve(models.size());
		for (const auto& [modelId, model] : models)
		{
			AssetSnapshot::Model modelSnapshot{};
			modelSnapshot.m_Id = modelId;
			modelSnapshot.m_ContentGeneration = model->m_ContentGeneration;
			modelSnapshot.m_ResidencyEpoch = model->m_ResidencyEpoch;
			modelSnapshot.m_LastUsedFrame = model->m_LastUsedFrame;
			modelSnapshot.m_UseCount = model->m_UseCount;
			modelSnapshot.m_State = model->m_State;
			modelSnapshot.m_ContentState = model->m_ContentState;
			modelSnapshot.m_ResidencyState = model->m_ResidencyState;
			modelSnapshot.m_ResidencyPolicy = model->m_ResidencyPolicy;
			modelSnapshot.m_SourcePath = model->m_SourcePath;
			modelSnapshot.m_Type = model->m_Type;
			modelSnapshot.m_Name = model->m_Name;
			modelSnapshot.m_ImportArtifactContentDigest = model->m_ImportArtifactContentDigest;
			modelSnapshot.m_IsImportArtifactCached =
				assetManager.m_ModelImportArtifactCache.Contains(
					model->m_ImportArtifactContentDigest);
			modelSnapshot.m_MeshInstanceCount = static_cast<uint32_t>(model->m_MeshInstance.size());
			if (const AssetDependencyModelState* dependencyState =
				assetManager.m_AssetDependencyGraph.FindModel(
					MakeAssetContentVersion(modelId, model->m_ContentGeneration)))
			{
				const AssetDependencyModelState& state = *dependencyState;
				modelSnapshot.m_DependencyCount =
					static_cast<uint32_t>(state.m_DependencyStates.size()) +
					state.m_StructuralFailureCount;
				modelSnapshot.m_ReadyDependencyCount = state.m_ReadyCount;
				modelSnapshot.m_PendingDependencyCount = state.m_PendingCount;
				modelSnapshot.m_FailedDependencyCount =
					state.m_FailedCount + state.m_StructuralFailureCount;
				modelSnapshot.m_CancelledDependencyCount = state.m_CancelledCount;
				modelSnapshot.m_DependencyEventUpdateCount = state.m_EventUpdateCount;
				modelSnapshot.m_HasDependencyState = true;
				GGLAB_ASSERT(modelSnapshot.m_DependencyCount ==
					modelSnapshot.m_ReadyDependencyCount +
					modelSnapshot.m_PendingDependencyCount +
					modelSnapshot.m_FailedDependencyCount +
					modelSnapshot.m_CancelledDependencyCount);
			}
			modelSnapshot.m_IsEvictionCandidate =
				isEvictionCandidate(AssetKind::Model, modelId.Value(), *model);
			recordResidency(*model, modelSnapshot.m_IsEvictionCandidate);
			snapshot.m_Models.emplace_back(std::move(modelSnapshot));
		}

		std::sort(snapshot.m_Models.begin(), snapshot.m_Models.end(),
			[](const AssetSnapshot::Model& lhs, const AssetSnapshot::Model& rhs)
			{ return lhs.m_Id.Value() < rhs.m_Id.Value(); });

		const MeshStore::EntryMap& meshes = assetManager.m_MeshStore.Entries();
		snapshot.m_Meshes.reserve(meshes.size());
		for (const auto& [meshId, mesh] : meshes)
		{
			snapshot.m_Meshes.push_back({
				.m_Id = meshId,
				.m_ContentGeneration = mesh->m_ContentGeneration,
				.m_ResidencyEpoch = mesh->m_ResidencyEpoch,
				.m_LastUsedFrame = mesh->m_LastUsedFrame,
				.m_UseCount = mesh->m_UseCount,
				.m_State = mesh->m_State,
				.m_ContentState = mesh->m_ContentState,
				.m_ResidencyState = mesh->m_ResidencyState,
				.m_ResidencyPolicy = mesh->m_ResidencyPolicy,
				.m_Name = mesh->m_Name,
				.m_VertexCount = mesh->m_VertexCount,
				.m_IndexCount = mesh->m_IndexCount,
				.m_IsUploaded = mesh->m_IsUploaded,
				.m_IsEvictionCandidate =
					isEvictionCandidate(AssetKind::Mesh, meshId.Value(), *mesh),
				});
			recordResidency(*mesh, snapshot.m_Meshes.back().m_IsEvictionCandidate);
		}
		std::sort(snapshot.m_Meshes.begin(), snapshot.m_Meshes.end(),
			[](const AssetSnapshot::Mesh& lhs, const AssetSnapshot::Mesh& rhs)
			{ return lhs.m_Id.Value() < rhs.m_Id.Value(); });

		const std::vector<TextureAssetReadInfo> textureInfos =
			assetManager.GetTextureAssetReadInfos();
		snapshot.m_Textures.reserve(textureInfos.size());
		for (const TextureAssetReadInfo& texture : textureInfos)
		{
			const AssetLifecycle& lifecycle = texture.m_Lifecycle;
			AssetSnapshot::Texture textureSnapshot{};
			textureSnapshot.m_Id = texture.m_Content.m_Id;
			textureSnapshot.m_ContentGeneration = texture.m_Content.m_Generation;
			textureSnapshot.m_ResidencyEpoch = lifecycle.m_ResidencyEpoch;
			textureSnapshot.m_ResidencyOperationSerial = lifecycle.m_ResidencyOperationSerial;
			textureSnapshot.m_LastUsedFrame = lifecycle.m_LastUsedFrame;
			textureSnapshot.m_UseCount = lifecycle.m_UseCount;
			textureSnapshot.m_State = lifecycle.m_State;
			textureSnapshot.m_ContentState = lifecycle.m_ContentState;
			textureSnapshot.m_ResidencyState = lifecycle.m_ResidencyState;
			textureSnapshot.m_ResidencyPolicy = lifecycle.m_ResidencyPolicy;
			textureSnapshot.m_SourcePath = texture.m_SourcePath;
			textureSnapshot.m_ImportSettings = texture.m_ImportSettings;
			textureSnapshot.m_Semantic = texture.m_Semantic;
			textureSnapshot.m_Name = texture.m_Name;
			textureSnapshot.m_Texture = texture.m_Texture;
			textureSnapshot.m_DebugName = texture.m_DebugName;
			textureSnapshot.m_ArtifactContentDigest = texture.m_ArtifactContentDigest;
			textureSnapshot.m_SourceDigest = texture.m_SourceDigest;
			textureSnapshot.m_DerivedDataKey = texture.m_DerivedDataKey;
			textureSnapshot.m_IsUploaded = texture.m_IsUploaded;
			textureSnapshot.m_HasSrv = texture.m_HasSrv;
			textureSnapshot.m_IsReserved = texture.m_IsReserved;
			textureSnapshot.m_IsCpuArtifactCached = texture.m_IsCpuArtifactCached;
			textureSnapshot.m_IsDerivedDataCached = texture.m_IsDerivedDataCached;
			textureSnapshot.m_IsEvictionCandidate =
				isEvictionCandidate(AssetKind::Texture, texture.m_Content.m_Id.Value(), lifecycle);
			recordResidency(lifecycle, textureSnapshot.m_IsEvictionCandidate);
			snapshot.m_Textures.emplace_back(std::move(textureSnapshot));
		}

		std::sort(snapshot.m_Textures.begin(), snapshot.m_Textures.end(),
			[](const AssetSnapshot::Texture& lhs, const AssetSnapshot::Texture& rhs)
			{ return lhs.m_Id.Value() < rhs.m_Id.Value(); });

		if (assetManager.m_AssetUploadScheduler)
		{
			const AssetUploadStatistics statistics =
				assetManager.m_AssetUploadScheduler->GetStatistics();
			snapshot.m_CpuPayloadQueue = statistics.m_CpuPayloadQueue;
			snapshot.m_ResourcePublicationQueue = statistics.m_ResourcePublicationQueue;
			snapshot.m_UploadRecordingQueue = statistics.m_UploadRecordingQueue;
			snapshot.m_GpuFinalizeQueue = statistics.m_GpuFinalizeQueue;
			snapshot.m_StreamingFrameBudget = statistics.m_FrameBudget;
			snapshot.m_LastStreamingFrameUsage = statistics.m_LastFrameUsage;
			snapshot.m_ReadyPayloadBytes = statistics.m_ReadyPayloadBytes;
			snapshot.m_ReadyPayloadHighWatermark = statistics.m_ReadyPayloadHighWatermark;
			snapshot.m_InFlightUploadBytes = statistics.m_InFlightBytes;
			snapshot.m_InFlightUploadHighWatermark = statistics.m_InFlightHighWatermark;
			snapshot.m_UploadPromotionBudgetDeferralCount =
				statistics.m_UploadPromotionBudgetDeferralCount;
			snapshot.m_UploadBudgetDeferralCount = statistics.m_UploadBudgetDeferralCount;
			snapshot.m_InFlightBudgetDeferralCount = statistics.m_InFlightBudgetDeferralCount;
			snapshot.m_OversizedAdmissionCount = statistics.m_OversizedAdmissionCount;
			snapshot.m_PendingUploadCount = statistics.m_PendingCount;
			snapshot.m_UploadBatchSubmissionCount = statistics.m_BatchSubmissionCount;
			snapshot.m_LastUploadBatchResourceCount = statistics.m_LastBatchUploadCount;
			snapshot.m_MaxResourcesPerUploadBatch = statistics.m_MaxUploadsPerBatch;
			snapshot.m_SubmittedUploadCount = statistics.m_SubmittedCount;
			snapshot.m_SucceededUploadCount = statistics.m_SucceededCount;
			snapshot.m_FailedUploadCount = statistics.m_FailedCount;
			snapshot.m_UploadCompletionCallbackFailureCount =
				statistics.m_CompletionCallbackFailureCount;

			const auto copyUploads = [](const std::vector<AssetUploadActivity>& source,
				std::vector<AssetSnapshot::Upload>& destination)
				{
					destination.reserve(source.size());
					for (const AssetUploadActivity& upload : source)
					{
						destination.push_back({
							.m_Handle = upload.m_Handle,
							.m_Name = upload.m_Name,
							.m_Identity = upload.m_Identity,
							.m_Estimate = upload.m_Estimate,
							.m_Status = upload.m_Status,
							.m_FencePoint = upload.m_FencePoint,
							.m_ElapsedMilliseconds = upload.m_ElapsedMilliseconds,
							.m_Progress = upload.m_Progress,
							});
					}
				};
			copyUploads(statistics.m_PendingUploads, snapshot.m_PendingUploads);
			copyUploads(statistics.m_RecentUploads, snapshot.m_RecentUploads);
		}

		const AssetOwnershipStatistics ownership = assetManager.GetOwnershipStatistics();
		snapshot.m_AssetOwnerCount = ownership.m_OwnerCount;
		snapshot.m_AssetLeaseCount = ownership.m_LeaseCount;
		snapshot.m_ManagedAssetCount = ownership.m_ManagedAssetCount;
		snapshot.m_OwnershipPriorityUpdateCount = ownership.m_PriorityUpdateCount;
		snapshot.m_OwnershipCpuCancellationCount = ownership.m_CpuCancellationCount;
		snapshot.m_OwnershipReadyCancellationCount = ownership.m_ReadyCancellationCount;
		snapshot.m_OwnershipGpuDeferredCancellationCount = ownership.m_GpuDeferredCancellationCount;
		snapshot.m_RuntimeRetirementRequestCount = ownership.m_RuntimeRetirementRequestCount;
		snapshot.m_RuntimeRetirementCancellationCount =
			ownership.m_RuntimeRetirementCancellationCount;
		snapshot.m_RuntimeRetirementCount = ownership.m_RuntimeRetirementCount;
		snapshot.m_PendingRuntimeRetirementCount = ownership.m_PendingRuntimeRetirementCount;
		snapshot.m_PublicationRetainCount = ownership.m_PublicationRetainCount;
		snapshot.m_PublicationProtectedCancellationCount =
			ownership.m_PublicationProtectedCancellationCount;
		snapshot.m_ActiveOwnershipInterests.reserve(ownership.m_ActiveInterests.size());
		for (const AssetInterestActivity& interest : ownership.m_ActiveInterests)
		{
			AssetStreamingWorkKind kind = AssetStreamingWorkKind::Unknown;
			switch (interest.m_Kind)
			{
			case AssetKind::Model:
				kind = AssetStreamingWorkKind::Model;
				break;
			case AssetKind::Texture:
				kind = AssetStreamingWorkKind::Texture;
				break;
			case AssetKind::Mesh:
				kind = AssetStreamingWorkKind::Mesh;
				break;
			}
			snapshot.m_ActiveOwnershipInterests.push_back({
				.m_Kind = kind,
				.m_StableId = interest.m_StableId,
				.m_Generation = interest.m_Generation,
				.m_LeaseCount = interest.m_LeaseCount,
				.m_OwnerCount = interest.m_OwnerCount,
				.m_EffectivePriority = interest.m_EffectivePriority,
				});
		}

		return snapshot;
	}
}
