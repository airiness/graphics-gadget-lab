#include "Core/Precompiled.h"
#include "Diagnostics/Builders/AssetSnapshotBuilder.h"
#include "Diagnostics/Snapshots/AssetSnapshot.h"
#include "Graphics/Asset/AssetIdentityConversions.h"
#include "Graphics/AssetManager.h"
#include "Graphics/AssetUploadScheduler.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/TextureRegistry.h"

#include <algorithm>

namespace gglab
{
	AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept
	{
		AssetSnapshot snapshot{};
		snapshot.m_AssetUsageFrame = assetManager.m_AssetUsageFrame;
		const AssetDependencyGraphStatistics dependencyStatistics =
			assetManager.m_AssetDependencyGraph.GetStatistics();
		snapshot.m_TrackedModelDependencyCount =
			dependencyStatistics.m_TrackedModelCount;
		snapshot.m_ReverseDependencyCount =
			dependencyStatistics.m_ReverseDependencyCount;
		snapshot.m_ReverseDependencyEdgeCount =
			dependencyStatistics.m_ReverseDependencyEdgeCount;
		snapshot.m_DependencyGraphBuildCount =
			dependencyStatistics.m_GraphBuildCount;
		snapshot.m_DependencyEventUpdateCount =
			dependencyStatistics.m_EventUpdateCount;
		snapshot.m_DependencyValidationCount = assetManager.m_DependencyValidationCount;
		snapshot.m_DependencyValidationMismatchCount =
			assetManager.m_DependencyValidationMismatchCount;
		const AssetResidencyStatistics residency =
			assetManager.GetResidencyStatistics();
		snapshot.m_AutomaticResidencyEvictionEnabled =
			residency.m_Config.m_EnableAutomaticEviction;
		snapshot.m_ResidencyHighWatermarkBytes =
			residency.m_Config.m_HighWatermarkBytes;
		snapshot.m_ResidencyLowWatermarkBytes =
			residency.m_Config.m_LowWatermarkBytes;
		snapshot.m_ResidencyMinUnusedFrames =
			residency.m_Config.m_MinUnusedFrames;
		snapshot.m_MaxResidencyEvictionsPerFrame =
			residency.m_Config.m_MaxEvictionsPerFrame;
		snapshot.m_LogicalResidentBytes = residency.m_LogicalResidentBytes;
		snapshot.m_PendingEvictionBytes = residency.m_PendingEvictionBytes;
		snapshot.m_PendingEvictionCount = residency.m_PendingEvictionCount;
		snapshot.m_ReloadingAssetCount = residency.m_ReloadingAssetCount;
		snapshot.m_ResidencyEvictionCount = residency.m_EvictionCount;
		snapshot.m_ResidencyEvictedBytes = residency.m_EvictedBytes;
		snapshot.m_ResidencyEvictionCancellationCount =
			residency.m_EvictionCancellationCount;
		snapshot.m_ResidencyReloadRequestCount = residency.m_ReloadRequestCount;
		snapshot.m_ResidencyReloadCoalescedCount = residency.m_ReloadCoalescedCount;
		snapshot.m_LastFrameReloadRequestCount =
			residency.m_LastFrameReloadRequestCount;
		snapshot.m_ReloadRequestHighWatermark =
			residency.m_ReloadRequestHighWatermark;

		const auto isEvictionCandidate = [&assetManager](
			AssetInterestKind kind,
			uint64_t stableId,
			const AssetLifecycle& lifecycle) noexcept
			{
				const AssetKind assetKind = kind == AssetInterestKind::Texture ?
					AssetKind::Texture : kind == AssetInterestKind::Mesh ?
					AssetKind::Mesh : AssetKind::Model;
				const AssetKey key = MakeAssetKey(assetKind, stableId);
				return lifecycle.m_ResidencyPolicy == AssetResidencyPolicy::Cacheable &&
					lifecycle.m_ResidencyState == AssetResidencyState::Resident &&
					!assetManager.HasActiveInterest(key) &&
					!assetManager.HasPublicationRetain(
						key,
						lifecycle.m_ContentGeneration);
			};
		const auto recordResidency = [&snapshot](
			const AssetLifecycle& lifecycle,
			bool evictionCandidate) noexcept
			{
				GGLAB_ASSERT_MSG(
					IsAssetLifecycleSynchronized(lifecycle),
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
			modelSnapshot.m_MeshInstanceCount = static_cast<uint32_t>(model->m_MeshInstance.size());
			if (const AssetDependencyModelState* dependencyState =
				assetManager.m_AssetDependencyGraph.FindModel(
					MakeAssetContentVersion(modelId, model->m_ContentGeneration)))
			{
				const AssetDependencyModelState& state = *dependencyState;
				modelSnapshot.m_DependencyCount = static_cast<uint32_t>(
					state.m_DependencyStates.size()) + state.m_StructuralFailureCount;
				modelSnapshot.m_ReadyDependencyCount = state.m_ReadyCount;
				modelSnapshot.m_PendingDependencyCount = state.m_PendingCount;
				modelSnapshot.m_FailedDependencyCount =
					state.m_FailedCount + state.m_StructuralFailureCount;
				modelSnapshot.m_CancelledDependencyCount = state.m_CancelledCount;
				modelSnapshot.m_DependencyEventUpdateCount = state.m_EventUpdateCount;
				modelSnapshot.m_HasDependencyState = true;
				GGLAB_ASSERT(
					modelSnapshot.m_DependencyCount ==
					modelSnapshot.m_ReadyDependencyCount +
					modelSnapshot.m_PendingDependencyCount +
					modelSnapshot.m_FailedDependencyCount +
					modelSnapshot.m_CancelledDependencyCount);
			}
			modelSnapshot.m_IsEvictionCandidate = isEvictionCandidate(
				AssetInterestKind::Model,
				modelId.Value(),
				*model);
			recordResidency(*model, modelSnapshot.m_IsEvictionCandidate);
			snapshot.m_Models.emplace_back(std::move(modelSnapshot));
		}

		std::sort(snapshot.m_Models.begin(), snapshot.m_Models.end(),
			[](const AssetSnapshot::Model& lhs, const AssetSnapshot::Model& rhs)
			{
				return lhs.m_Id.Value() < rhs.m_Id.Value();
			});

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
				.m_IsEvictionCandidate = isEvictionCandidate(
					AssetInterestKind::Mesh,
					meshId.Value(),
					*mesh),
			});
			recordResidency(*mesh, snapshot.m_Meshes.back().m_IsEvictionCandidate);
		}
		std::sort(snapshot.m_Meshes.begin(), snapshot.m_Meshes.end(),
			[](const AssetSnapshot::Mesh& lhs, const AssetSnapshot::Mesh& rhs)
			{
				return lhs.m_Id.Value() < rhs.m_Id.Value();
			});

		const TextureRegistry* textureRegistry = assetManager.m_TextureRegistry;
		if (textureRegistry)
		{
			snapshot.m_Textures.reserve(textureRegistry->m_TextureContainer.m_TextureIDMap.size());
			for (const auto& [textureId, texture] : textureRegistry->m_TextureContainer.m_TextureIDMap)
			{
				AssetSnapshot::Texture textureSnapshot{};
				textureSnapshot.m_Id = textureId;
				textureSnapshot.m_ContentGeneration = texture->m_ContentGeneration;
				textureSnapshot.m_ResidencyEpoch = texture->m_ResidencyEpoch;
				textureSnapshot.m_LastUsedFrame = texture->m_LastUsedFrame;
				textureSnapshot.m_UseCount = texture->m_UseCount;
				textureSnapshot.m_State = texture->m_State;
				textureSnapshot.m_ContentState = texture->m_ContentState;
				textureSnapshot.m_ResidencyState = texture->m_ResidencyState;
				textureSnapshot.m_ResidencyPolicy = texture->m_ResidencyPolicy;
				textureSnapshot.m_SourcePath = texture->m_SourcePath;
				textureSnapshot.m_Semantic = texture->m_Semantic;
				textureSnapshot.m_Name = texture->m_Name;
				textureSnapshot.m_Texture = texture->m_Texture;
				textureSnapshot.m_DebugName = textureRegistry->m_Device ?
					std::string(textureRegistry->m_Device->GetTextureDebugName(texture->m_Texture)) :
					std::string{};
				textureSnapshot.m_IsUploaded = texture->m_IsUploaded;
				textureSnapshot.m_IsReserved = IsReservedTextureId(textureId);
				textureSnapshot.m_IsEvictionCandidate = isEvictionCandidate(
					AssetInterestKind::Texture,
					textureId.Value(),
					*texture);
				recordResidency(*texture, textureSnapshot.m_IsEvictionCandidate);
				snapshot.m_Textures.emplace_back(std::move(textureSnapshot));
			}

			std::sort(snapshot.m_Textures.begin(), snapshot.m_Textures.end(),
				[](const AssetSnapshot::Texture& lhs, const AssetSnapshot::Texture& rhs)
				{
					return lhs.m_Id.Value() < rhs.m_Id.Value();
				});
		}

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

			const auto copyUploads = [](
				const std::vector<AssetUploadActivity>& source,
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
		snapshot.m_OwnershipGpuDeferredCancellationCount =
			ownership.m_GpuDeferredCancellationCount;
		snapshot.m_OwnershipReadyRetentionCount = ownership.m_ReadyRetentionCount;
		snapshot.m_PublicationRetainCount = ownership.m_PublicationRetainCount;
		snapshot.m_PublicationProtectedCancellationCount =
			ownership.m_PublicationProtectedCancellationCount;
		snapshot.m_ActiveOwnershipInterests.reserve(ownership.m_ActiveInterests.size());
		for (const AssetInterestActivity& interest : ownership.m_ActiveInterests)
		{
			AssetStreamingWorkKind kind = AssetStreamingWorkKind::Unknown;
			switch (interest.m_Kind)
			{
			case AssetInterestKind::Model: kind = AssetStreamingWorkKind::Model; break;
			case AssetInterestKind::Texture: kind = AssetStreamingWorkKind::Texture; break;
			case AssetInterestKind::Mesh: kind = AssetStreamingWorkKind::Mesh; break;
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
