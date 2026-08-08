#pragma once

#include "Application/Lab/NapaVoxel/NapaVoxelMeshAdapter.h"
#include "Graphics/Asset/Streaming/AssetUploadScheduler.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIResource.h"

#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace gglab
{
	class NapaVoxelCommandQueue;
	class NapaVoxelInitialPublicationOwner;
	class NapaVoxelPreparedInitialCommit;
	class GGLabMeshPublicationBatch;

	[[nodiscard]] bool PrepareNapaVoxelInitialPublication(
		std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pendingCoreMeshes,
		uint64_t stableId, uint64_t ownerGeneration,
		std::shared_ptr<NapaVoxelInitialPublicationOwner>& publication) noexcept;

	enum class NapaVoxelInitialPublicationStatus : uint8_t
	{
		Uninitialized,
		Prepared,
		Queued,
		Recording,
		AwaitingFence,
		ReadyForCommit,
		Committed,
		Failed,
		Cancelled,
	};

	struct NapaVoxelGpuSectionDraw
	{
		napa::voxel::VoxelMaterial m_Material = napa::voxel::VoxelMaterial::Empty;
		uint32_t m_FirstIndex = 0;
		uint32_t m_IndexCount = 0;
	};

	struct NapaVoxelGpuChunkMesh
	{
		napa::voxel::ChunkCoord m_Chunk{};
		Vector3 m_Translation{};
		RHIBufferDesc m_VertexBufferDesc{};
		RHIBufferDesc m_IndexBufferDesc{};
		RHIBufferOwner m_VertexBuffer;
		RHIBufferOwner m_IndexBuffer;
		std::vector<NapaVoxelGpuSectionDraw> m_Sections;
	};

	struct NapaVoxelGpuChunkReplacement
	{
		napa::voxel::ChunkCoord m_Chunk{};
		std::shared_ptr<const NapaVoxelGpuChunkMesh> m_Mesh;

		[[nodiscard]] bool IsDelete() const noexcept { return !m_Mesh; }
	};

	class NapaVoxelGpuMeshSet final
	{
	public:
		NapaVoxelGpuMeshSet() noexcept = default;
		GGLAB_DELETE_COPYABLE(NapaVoxelGpuMeshSet);
		NapaVoxelGpuMeshSet(NapaVoxelGpuMeshSet&&) noexcept = default;
		NapaVoxelGpuMeshSet& operator=(NapaVoxelGpuMeshSet&&) noexcept = default;

		[[nodiscard]] uint64_t GetVisibleWorldRevision() const noexcept
		{
			return m_VisibleWorldRevision;
		}
		[[nodiscard]] const napa::voxel::VoxelWorldConfig& GetConfig() const noexcept
		{
			return m_Config;
		}
		[[nodiscard]] const std::vector<std::shared_ptr<const NapaVoxelGpuChunkMesh>>&
			GetChunks() const noexcept
		{
			return m_Chunks;
		}

	private:
		friend class NapaVoxelInitialPublicationOwner;
		friend class GGLabMeshPublicationBatch;
		friend class NapaVoxelRenderState;

		napa::voxel::VoxelWorldConfig m_Config{};
		uint64_t m_VisibleWorldRevision = 0;
		std::vector<std::shared_ptr<const NapaVoxelGpuChunkMesh>> m_Chunks;
	};

	enum class GGLabMeshPublicationStatus : uint8_t
	{
		Uninitialized,
		Prepared,
		Queued,
		Recording,
		AwaitingFence,
		ReadyForCommit,
		Failed,
		Cancelled,
	};

	struct GGLabMeshPublicationIdentity
	{
		uint64_t m_OperationSerial = 0;
		uint64_t m_PublicationSerial = 0;
		uint64_t m_OwnerGeneration = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const GGLabMeshPublicationIdentity&,
			const GGLabMeshPublicationIdentity&) noexcept = default;
	};

	[[nodiscard]] bool PrepareGGLabMeshPublicationBatch(
		std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pendingCoreMeshes,
		const std::shared_ptr<const NapaVoxelGpuMeshSet>& visibleGpuMeshes,
		GGLabMeshPublicationIdentity identity, uint64_t schedulerStableId,
		std::shared_ptr<GGLabMeshPublicationBatch>& publication) noexcept;

	class GGLabMeshPublicationBatch final
	{
	private:
		struct ConstructionToken
		{
		};

	public:
		GGLabMeshPublicationBatch(ConstructionToken,
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pendingCoreMeshes,
			NapaVoxelCpuMeshSet cpuReplacements,
			std::shared_ptr<const NapaVoxelGpuMeshSet> baseGpuMeshes,
			GGLabMeshPublicationIdentity identity, uint64_t schedulerStableId) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(GGLabMeshPublicationBatch);

		[[nodiscard]] bool PrepareGpuResources(RHIDevice* device) noexcept;
		[[nodiscard]] bool MarkQueued() noexcept;
		[[nodiscard]] bool BeginRecording(const AssetStreamingIdentity& identity) noexcept;
		[[nodiscard]] bool RecordUpload(TransferBatch& batch) noexcept;
		[[nodiscard]] bool SetUploadHandle(AssetUploadHandle handle) noexcept;
		[[nodiscard]] bool CompleteUpload(
			const AssetUploadCompletionInfo& completion) noexcept;
		void Fail() noexcept;
		void Cancel() noexcept;

		[[nodiscard]] GGLabMeshPublicationStatus GetStatus() const noexcept
		{
			return m_Status;
		}
		[[nodiscard]] const GGLabMeshPublicationIdentity& GetIdentity() const noexcept
		{
			return m_Identity;
		}
		[[nodiscard]] AssetStreamingIdentity GetUploadIdentity() const noexcept
		{
			return m_UploadIdentity;
		}
		[[nodiscard]] AssetUploadHandle GetUploadHandle() const noexcept
		{
			return m_UploadHandle;
		}
		[[nodiscard]] RHIFencePoint GetCompletionFence() const noexcept
		{
			return m_CompletionFence;
		}
		[[nodiscard]] const AssetStreamingWorkEstimate& GetUploadEstimate() const noexcept
		{
			return m_UploadEstimate;
		}
		[[nodiscard]] uint64_t GetBaseWorldRevision() const noexcept;
		[[nodiscard]] uint64_t GetTargetWorldRevision() const noexcept;
		[[nodiscard]] const std::vector<NapaVoxelGpuChunkReplacement>&
			GetReplacements() const noexcept
		{
			return m_Replacements;
		}
		[[nodiscard]] const std::shared_ptr<const NapaVoxelGpuMeshSet>&
			GetBaseGpuMeshes() const noexcept
		{
			return m_BaseGpuMeshes;
		}
		[[nodiscard]] std::shared_ptr<const NapaVoxelGpuMeshSet>
			GetProspectiveGpuMeshes() const noexcept
		{
			return m_ProspectiveGpuMeshes;
		}
		[[nodiscard]] bool IsReadyForCommit() const noexcept
		{
			return m_Status == GGLabMeshPublicationStatus::ReadyForCommit;
		}
		[[nodiscard]] bool HasCompletion() const noexcept { return m_HasCompletion; }
		[[nodiscard]] bool IsOwnerGenerationExhausted() const noexcept
		{
			return m_OwnerGenerationExhausted;
		}

	private:
		friend bool PrepareGGLabMeshPublicationBatch(
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pendingCoreMeshes,
			const std::shared_ptr<const NapaVoxelGpuMeshSet>& visibleGpuMeshes,
			GGLabMeshPublicationIdentity identity, uint64_t schedulerStableId,
			std::shared_ptr<GGLabMeshPublicationBatch>& publication) noexcept;

		std::unique_ptr<napa::voxel::PendingCpuMeshBatch> m_PendingCoreMeshes;
		NapaVoxelCpuMeshSet m_CpuReplacements;
		std::shared_ptr<const NapaVoxelGpuMeshSet> m_BaseGpuMeshes;
		std::shared_ptr<NapaVoxelGpuMeshSet> m_ProspectiveGpuMeshes;
		std::vector<NapaVoxelGpuChunkReplacement> m_Replacements;
		GGLabMeshPublicationIdentity m_Identity{};
		AssetStreamingIdentity m_UploadIdentity{};
		AssetStreamingWorkEstimate m_UploadEstimate{};
		AssetUploadHandle m_UploadHandle{};
		RHIFencePoint m_CompletionFence{};
		GGLabMeshPublicationStatus m_Status = GGLabMeshPublicationStatus::Uninitialized;
		bool m_PublicationAllowed = true;
		bool m_OwnerGenerationExhausted = false;
		bool m_HasCompletion = false;
	};

	struct NapaVoxelPublicationSerialState
	{
		uint64_t m_LastPublicationSerial = 0;
	};

	class NapaVoxelMeshReplacementUploadSession final
	{
	public:
		NapaVoxelMeshReplacementUploadSession(RHIDevice* device,
			AssetUploadScheduler* scheduler, NapaVoxelCommandQueue* commandQueue,
			NapaVoxelPublicationSerialState serialState = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(NapaVoxelMeshReplacementUploadSession);
		~NapaVoxelMeshReplacementUploadSession();

		[[nodiscard]] bool BeginPrepare(
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pendingCoreMeshes,
			const std::shared_ptr<const NapaVoxelGpuMeshSet>& visibleGpuMeshes,
			uint64_t operationSerial, uint64_t ownerGeneration) noexcept;
		void TickPrepare() noexcept;
		void CancelPrepare() noexcept;

		[[nodiscard]] bool IsReady() const noexcept { return m_IsReady; }
		[[nodiscard]] bool HasFailed() const noexcept { return m_HasFailed; }
		[[nodiscard]] uint64_t GetLastPublicationSerial() const noexcept
		{
			return m_LastPublicationSerial;
		}
		[[nodiscard]] const std::shared_ptr<GGLabMeshPublicationBatch>&
			GetPublication() const noexcept
		{
			return m_Publication;
		}

	private:
		void ScheduleUpload() noexcept;
		void FailHostPreparation(bool publicationSerialExhausted) noexcept;

		RHIDevice* m_Device = nullptr;
		AssetUploadScheduler* m_Scheduler = nullptr;
		NapaVoxelCommandQueue* m_CommandQueue = nullptr;
		std::shared_ptr<GGLabMeshPublicationBatch> m_Publication;
		uint64_t m_LastPublicationSerial = 0;
		bool m_IsReady = false;
		bool m_HasFailed = false;
	};

	class NapaVoxelInitialPublicationOwner final
	{
	private:
		struct ConstructionToken
		{
		};

	public:
		NapaVoxelInitialPublicationOwner(ConstructionToken,
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pendingCoreMeshes,
			NapaVoxelCpuMeshSet cpuMeshes, uint64_t stableId, uint64_t ownerGeneration) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(NapaVoxelInitialPublicationOwner);
		~NapaVoxelInitialPublicationOwner() = default;

		[[nodiscard]] bool PrepareGpuResources(RHIDevice* device) noexcept;
		[[nodiscard]] bool MarkQueued() noexcept;
		[[nodiscard]] bool BeginRecording(const AssetStreamingIdentity& identity) noexcept;
		[[nodiscard]] bool RecordUpload(TransferBatch& batch) noexcept;
		[[nodiscard]] bool SetUploadHandle(AssetUploadHandle handle) noexcept;
		[[nodiscard]] bool CompleteUpload(const AssetUploadCompletionInfo& completion) noexcept;
		void Fail() noexcept;
		void Cancel() noexcept;

		[[nodiscard]] NapaVoxelInitialPublicationStatus GetStatus() const noexcept
		{
			return m_Status;
		}
		[[nodiscard]] AssetStreamingIdentity GetUploadIdentity() const noexcept
		{
			return m_UploadIdentity;
		}
		[[nodiscard]] AssetUploadHandle GetUploadHandle() const noexcept
		{
			return m_UploadHandle;
		}
		[[nodiscard]] RHIFencePoint GetCompletionFence() const noexcept
		{
			return m_CompletionFence;
		}
		[[nodiscard]] const AssetStreamingWorkEstimate& GetUploadEstimate() const noexcept
		{
			return m_UploadEstimate;
		}
		[[nodiscard]] uint64_t GetTargetWorldRevision() const noexcept;
		[[nodiscard]] uint64_t GetOwnerGeneration() const noexcept
		{
			return m_OwnerGeneration;
		}
		[[nodiscard]] bool IsOwnerGenerationExhausted() const noexcept
		{
			return m_OwnerGenerationExhausted;
		}
		[[nodiscard]] bool IsReadyForCommit() const noexcept
		{
			return m_Status == NapaVoxelInitialPublicationStatus::ReadyForCommit;
		}
		[[nodiscard]] bool HasCompletion() const noexcept { return m_HasCompletion; }

	private:
		friend bool PrepareNapaVoxelInitialPublication(
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pendingCoreMeshes,
			uint64_t stableId, uint64_t ownerGeneration,
			std::shared_ptr<NapaVoxelInitialPublicationOwner>& publication) noexcept;
		friend class NapaVoxelRenderState;
		void MarkCommitted() noexcept;

		napa::voxel::VoxelWorldConfig m_Config{};
		uint64_t m_TargetWorldRevision = 0;
		std::unique_ptr<napa::voxel::PendingCpuMeshBatch> m_PendingCoreMeshes;
		NapaVoxelCpuMeshSet m_CpuMeshes;
		std::shared_ptr<NapaVoxelGpuMeshSet> m_GpuMeshes;
		AssetStreamingIdentity m_UploadIdentity{};
		AssetStreamingWorkEstimate m_UploadEstimate{};
		AssetUploadHandle m_UploadHandle{};
		RHIFencePoint m_CompletionFence{};
		uint64_t m_OwnerGeneration = 0;
		NapaVoxelInitialPublicationStatus m_Status =
			NapaVoxelInitialPublicationStatus::Uninitialized;
		bool m_PublicationAllowed = true;
		bool m_OwnerGenerationExhausted = false;
		bool m_HasCompletion = false;
	};

	class NapaVoxelPreparedInitialCommit final
	{
	private:
		struct ConstructionToken
		{
		};

	public:
		explicit NapaVoxelPreparedInitialCommit(ConstructionToken) noexcept {}
		GGLAB_DELETE_COPYABLE_MOVABLE(NapaVoxelPreparedInitialCommit);

	private:
		friend class NapaVoxelRenderState;

		std::unique_ptr<napa::voxel::PreparedCpuMeshPublication> m_CorePublication;
		std::shared_ptr<const NapaVoxelGpuMeshSet> m_GpuMeshes;
		std::shared_ptr<NapaVoxelInitialPublicationOwner> m_Owner;
	};

	class NapaVoxelRenderState final
	{
	public:
		[[nodiscard]] bool PrepareInitialCommit(
			const std::shared_ptr<NapaVoxelInitialPublicationOwner>& publication,
			std::unique_ptr<NapaVoxelPreparedInitialCommit>& preparedCommit) noexcept;
		void CommitInitial(
			std::unique_ptr<NapaVoxelPreparedInitialCommit>& preparedCommit) noexcept;
		void Reset() noexcept;

		[[nodiscard]] bool HasVisibleMeshes() const noexcept
		{
			return m_VisibleCoreMeshes.HasPublishedMeshes() && m_VisibleGpuMeshes != nullptr;
		}
		[[nodiscard]] uint64_t GetVisibleWorldRevision() const noexcept
		{
			return m_VisibleCoreMeshes.GetVisibleWorldRevision();
		}
		[[nodiscard]] const napa::voxel::VisibleMeshSet& GetVisibleCoreMeshes() const noexcept
		{
			return m_VisibleCoreMeshes;
		}
		[[nodiscard]] const std::shared_ptr<const NapaVoxelGpuMeshSet>&
			GetVisibleGpuMeshes() const noexcept
		{
			return m_VisibleGpuMeshes;
		}

	private:
		napa::voxel::VisibleMeshSet m_VisibleCoreMeshes;
		std::shared_ptr<const NapaVoxelGpuMeshSet> m_VisibleGpuMeshes;
	};

	class NapaVoxelStaticPublicationSession final
	{
	public:
		NapaVoxelStaticPublicationSession(
			RHIDevice* device, AssetUploadScheduler* scheduler) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(NapaVoxelStaticPublicationSession);
		~NapaVoxelStaticPublicationSession();

		[[nodiscard]] bool BeginPrepare(
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pendingCoreMeshes,
			uint64_t stableId, uint64_t ownerGeneration) noexcept;
		void TickPrepare() noexcept;
		void CancelPrepare() noexcept;

		[[nodiscard]] bool IsReady() const noexcept { return m_IsReady; }
		[[nodiscard]] bool HasFailed() const noexcept { return m_HasFailed; }
		[[nodiscard]] bool HasVisibleMeshes() const noexcept
		{
			return m_RenderState.HasVisibleMeshes();
		}
		[[nodiscard]] uint64_t GetVisibleWorldRevision() const noexcept
		{
			return m_RenderState.GetVisibleWorldRevision();
		}
		[[nodiscard]] NapaVoxelInitialPublicationStatus GetPublicationStatus() const noexcept
		{
			return m_Publication
				? m_Publication->GetStatus()
				: NapaVoxelInitialPublicationStatus::Uninitialized;
		}
		[[nodiscard]] std::shared_ptr<const NapaVoxelGpuMeshSet> GetFrameView() const noexcept
		{
			return m_FrameView;
		}
		[[nodiscard]] const napa::voxel::VisibleMeshSet& GetVisibleCoreMeshes() const noexcept
		{
			return m_RenderState.GetVisibleCoreMeshes();
		}

	private:
		void ScheduleUpload() noexcept;

		RHIDevice* m_Device = nullptr;
		AssetUploadScheduler* m_Scheduler = nullptr;
		std::shared_ptr<NapaVoxelInitialPublicationOwner> m_Publication;
		NapaVoxelRenderState m_RenderState;
		std::shared_ptr<const NapaVoxelGpuMeshSet> m_FrameView;
		bool m_IsReady = false;
		bool m_HasFailed = false;
	};

	[[nodiscard]] bool AllocateNapaVoxelPublicationStableId(uint64_t& stableId) noexcept;
}
