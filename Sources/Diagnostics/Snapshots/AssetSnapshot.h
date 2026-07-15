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
			AssetUploadStatus m_Status = AssetUploadStatus::Pending;
			RHIFencePoint m_FencePoint{};
			double m_ElapsedMilliseconds = 0.0;
			ProgressSnapshot m_Progress;
		};

		std::vector<Model> m_Models;
		std::vector<Mesh> m_Meshes;
		std::vector<Texture> m_Textures;
		AssetStreamingQueueStatistics m_CpuReadyQueue;
		AssetStreamingQueueStatistics m_UploadReadyQueue;
		AssetStreamingQueueStatistics m_PublicationReadyQueue;
		uint32_t m_PendingUploadCount = 0;
		uint64_t m_SubmittedUploadCount = 0;
		uint64_t m_SucceededUploadCount = 0;
		uint64_t m_FailedUploadCount = 0;
		uint64_t m_UploadCompletionCallbackFailureCount = 0;
		std::vector<Upload> m_PendingUploads;
		std::vector<Upload> m_RecentUploads;
	};

	template<>
	struct SnapshotTraits<AssetSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.AssetSnapshot");
	};
}
