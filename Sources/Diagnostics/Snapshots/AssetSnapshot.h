#pragma once
#include "Diagnostics/SnapshotCommon.h"
#include "Graphics/AssetUploadScheduler.h"
#include "Graphics/GraphicsTypes.h"

namespace gglab
{
	struct AssetSnapshot
	{
		struct Model
		{
			ModelID m_Id{};
			uint64_t m_Generation = 0;
			AssetState m_State = AssetState::Unloaded;
			std::filesystem::path m_SourcePath;
			ModelType m_Type = ModelType::Invalid;
			StringID m_Name{};
			uint32_t m_MeshInstanceCount = 0;
		};

		struct Mesh
		{
			MeshID m_Id{};
			uint64_t m_Generation = 0;
			AssetState m_State = AssetState::Unloaded;
			StringID m_Name{};
			uint32_t m_VertexCount = 0;
			uint32_t m_IndexCount = 0;
			bool m_IsUploaded = false;
		};

		struct Texture
		{
			TextureID m_Id{};
			uint64_t m_Generation = 0;
			AssetState m_State = AssetState::Unloaded;
			std::filesystem::path m_SourcePath;
			TextureSemantic m_Semantic = TextureSemantic::Unknown;
			StringID m_Name{};
			RHITextureHandle m_Texture{};
			std::string m_DebugName;
			bool m_IsUploaded = false;
			bool m_IsReserved = false;
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
