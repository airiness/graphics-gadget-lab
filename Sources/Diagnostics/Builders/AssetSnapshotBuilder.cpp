#include "Core/Precompiled.h"
#include "Diagnostics/Builders/AssetSnapshotBuilder.h"
#include "Diagnostics/Snapshots/AssetSnapshot.h"
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

		const auto findModelSourcePath = [&assetManager](ModelID modelId) -> std::filesystem::path
			{
				for (const auto& [path, pathModelId] : assetManager.m_ModelContainer.m_PathIDMap)
				{
					if (pathModelId == modelId)
					{
						return path;
					}
				}
				return {};
			};

		snapshot.m_Models.reserve(assetManager.m_ModelContainer.m_ModelIDMap.size());
		for (const auto& [modelId, model] : assetManager.m_ModelContainer.m_ModelIDMap)
		{
			AssetSnapshot::Model modelSnapshot{};
			modelSnapshot.m_Id = modelId;
			modelSnapshot.m_Generation = model->m_Generation;
			modelSnapshot.m_State = model->m_State;
			modelSnapshot.m_SourcePath = findModelSourcePath(modelId);
			modelSnapshot.m_Type = model->m_Type;
			modelSnapshot.m_Name = model->m_Name;
			modelSnapshot.m_MeshInstanceCount = static_cast<uint32_t>(model->m_MeshInstance.size());
			snapshot.m_Models.emplace_back(std::move(modelSnapshot));
		}

		std::sort(snapshot.m_Models.begin(), snapshot.m_Models.end(),
			[](const AssetSnapshot::Model& lhs, const AssetSnapshot::Model& rhs)
			{
				return lhs.m_Id.Value() < rhs.m_Id.Value();
			});

		snapshot.m_Meshes.reserve(assetManager.m_MeshContainer.m_MeshIDMap.size());
		for (const auto& [meshId, mesh] : assetManager.m_MeshContainer.m_MeshIDMap)
		{
			snapshot.m_Meshes.push_back({
				.m_Id = meshId,
				.m_Generation = mesh->m_Generation,
				.m_State = mesh->m_State,
				.m_Name = mesh->m_Name,
				.m_VertexCount = mesh->m_VertexCount,
				.m_IndexCount = mesh->m_IndexCount,
				.m_IsUploaded = mesh->m_IsUploaded,
			});
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
				textureSnapshot.m_Generation = texture->m_Generation;
				textureSnapshot.m_State = texture->m_State;
				textureSnapshot.m_SourcePath = texture->m_SourcePath;
				textureSnapshot.m_Semantic = texture->m_Semantic;
				textureSnapshot.m_Name = texture->m_Name;
				textureSnapshot.m_Texture = texture->m_Texture;
				textureSnapshot.m_DebugName = textureRegistry->m_Device ?
					std::string(textureRegistry->m_Device->GetTextureDebugName(texture->m_Texture)) :
					std::string{};
				textureSnapshot.m_IsUploaded = texture->m_IsUploaded;
				textureSnapshot.m_IsReserved = IsReservedTextureId(textureId);
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
			snapshot.m_BacklogBudgetDeferralCount = statistics.m_BacklogBudgetDeferralCount;
			snapshot.m_UploadBudgetDeferralCount = statistics.m_UploadBudgetDeferralCount;
			snapshot.m_InFlightBudgetDeferralCount = statistics.m_InFlightBudgetDeferralCount;
			snapshot.m_OversizedAdmissionCount = statistics.m_OversizedAdmissionCount;
			snapshot.m_PendingUploadCount = statistics.m_PendingCount;
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
