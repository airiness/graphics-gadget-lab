#include "Core/Precompiled.h"
#include "Application/Lab/NapaVoxel/NapaVoxelRenderState.h"

#include "Application/Lab/NapaVoxel/NapaVoxelCommands.h"

#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/TransferBatch.h"

#include "NapaVoxelCore/Edit/VoxelMutation.h"

#include <atomic>
#include <limits>
#include <type_traits>

namespace gglab
{
	namespace
	{
		std::atomic<uint64_t> NextNapaVoxelPublicationStableId = 1;

		static_assert(std::is_nothrow_move_assignable_v<
			std::shared_ptr<const NapaVoxelGpuMeshSet>>);
		static_assert(std::is_nothrow_move_assignable_v<
			std::unique_ptr<const napa::voxel::VoxelDamageMarkerSnapshot>>);
		static_assert(std::is_nothrow_move_assignable_v<
			std::unique_ptr<NapaVoxelRetiredGpuMeshSet>>);

		[[nodiscard]] bool CheckedAccumulate(uint64_t value, uint64_t& total) noexcept
		{
			if (value > std::numeric_limits<uint64_t>::max() - total)
			{
				return false;
			}
			total += value;
			return true;
		}

		[[nodiscard]] bool IsValidPublicationInput(
			const napa::voxel::PendingCpuMeshBatch* pendingCoreMeshes,
			uint64_t stableId, uint64_t ownerGeneration) noexcept
		{
			return pendingCoreMeshes && stableId != 0 && ownerGeneration != 0 &&
				napa::voxel::ValidateConfig(pendingCoreMeshes->GetConfig()).Succeeded() &&
				pendingCoreMeshes->GetTargetWorldVoxelRevision() != 0;
		}

		[[nodiscard]] bool IsChunkLess(
			napa::voxel::ChunkCoord lhs, napa::voxel::ChunkCoord rhs) noexcept
		{
			return napa::voxel::ChunkCoordZYXLess{}(lhs, rhs);
		}

		[[nodiscard]] bool PrepareGpuChunkMesh(RHIDevice* device,
			const napa::voxel::VoxelWorldConfig& config,
			const NapaVoxelCpuChunkMesh& cpuChunk, const AssetStreamingIdentity& uploadIdentity,
			std::string_view label, std::shared_ptr<const NapaVoxelGpuChunkMesh>& mesh,
			uint64_t& vertexBytes, uint64_t& indexBytes) noexcept
		{
			if (!device || cpuChunk.IsEmpty())
			{
				return false;
			}

			const uint64_t preparedVertexBytes =
				static_cast<uint64_t>(cpuChunk.m_Vertices.size()) * sizeof(NapaVoxelRenderVertex);
			const uint64_t preparedIndexBytes =
				static_cast<uint64_t>(cpuChunk.m_Indices.size()) * sizeof(uint32_t);
			if (preparedVertexBytes == 0 || preparedIndexBytes == 0 ||
				preparedVertexBytes > std::numeric_limits<uint32_t>::max() ||
				preparedIndexBytes > std::numeric_limits<uint32_t>::max())
			{
				return false;
			}

			Vector3 translation{};
			if (ComputeNapaVoxelRenderTranslation(
				config, cpuChunk.m_ChunkOrigin, {}, translation).Failed())
			{
				return false;
			}

			try
			{
				auto prepared = std::make_shared<NapaVoxelGpuChunkMesh>();
				prepared->m_Chunk = cpuChunk.m_Chunk;
				prepared->m_Translation = translation;
				prepared->m_VertexBufferDesc = {
					.m_SizeInBytes = preparedVertexBytes,
					.m_StrideInBytes = sizeof(NapaVoxelRenderVertex),
					.m_Usage = RHIBufferUsage::Vertex | RHIBufferUsage::CopyDest,
					.m_DebugName = "NapaVoxel.VertexBuffer",
				};
				prepared->m_IndexBufferDesc = {
					.m_SizeInBytes = preparedIndexBytes,
					.m_StrideInBytes = sizeof(uint32_t),
					.m_Usage = RHIBufferUsage::Index | RHIBufferUsage::CopyDest,
					.m_DebugName = "NapaVoxel.IndexBuffer",
				};

				const RHIResourceDebugIdentityDesc vertexIdentity{
					.m_Domain = RHIResourceDebugDomain::Renderer,
					.m_Category = "NapaVoxel.VertexBuffer",
					.m_Label = label,
					.m_StableId = uploadIdentity.m_StableId,
				};
				const RHIResourceDebugIdentityDesc indexIdentity{
					.m_Domain = RHIResourceDebugDomain::Renderer,
					.m_Category = "NapaVoxel.IndexBuffer",
					.m_Label = label,
					.m_StableId = uploadIdentity.m_StableId,
				};
				prepared->m_VertexBuffer = RHIBufferOwner(
					device, device->CreateBuffer(prepared->m_VertexBufferDesc, vertexIdentity));
				prepared->m_IndexBuffer = RHIBufferOwner(
					device, device->CreateBuffer(prepared->m_IndexBufferDesc, indexIdentity));
				if (!prepared->m_VertexBuffer || !prepared->m_IndexBuffer)
				{
					return false;
				}

				prepared->m_Sections.reserve(cpuChunk.m_SectionDrawRanges.size());
				for (const NapaVoxelSectionDrawRange& section : cpuChunk.m_SectionDrawRanges)
				{
					prepared->m_Sections.push_back({
						.m_Material = section.m_Material,
						.m_FirstIndex = section.m_FirstIndex,
						.m_IndexCount = section.m_IndexCount,
						});
				}

				vertexBytes = preparedVertexBytes;
				indexBytes = preparedIndexBytes;
				mesh = std::move(prepared);
				return true;
			}
			catch (...)
			{
				return false;
			}
		}
	}

	GGLabMeshPublicationBatch::GGLabMeshPublicationBatch(ConstructionToken,
		std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pendingCoreMeshes,
		NapaVoxelCpuMeshSet cpuReplacements,
		std::shared_ptr<const NapaVoxelGpuMeshSet> baseGpuMeshes,
		GGLabMeshPublicationIdentity identity, uint64_t schedulerStableId) noexcept :
		m_PendingCoreMeshes(std::move(pendingCoreMeshes)),
		m_CpuReplacements(std::move(cpuReplacements)),
		m_BaseGpuMeshes(std::move(baseGpuMeshes)),
		m_Identity(identity),
		m_BaseWorldRevision(m_PendingCoreMeshes->GetBaseWorldVoxelRevision()),
		m_TargetWorldRevision(m_PendingCoreMeshes->GetTargetWorldVoxelRevision()),
		m_UploadIdentity({
			.m_Kind = AssetStreamingWorkKind::RuntimeMesh,
			.m_StableId = schedulerStableId,
			.m_Generation = identity.m_OwnerGeneration,
			})
	{
	}

	bool PrepareGGLabMeshPublicationBatch(
		std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pendingCoreMeshes,
		const std::shared_ptr<const NapaVoxelGpuMeshSet>& visibleGpuMeshes,
		GGLabMeshPublicationIdentity identity, uint64_t schedulerStableId,
		std::shared_ptr<GGLabMeshPublicationBatch>& publication) noexcept
	{
		if (!pendingCoreMeshes || !visibleGpuMeshes || identity.m_OperationSerial == 0 ||
			identity.m_PublicationSerial == 0 || identity.m_OwnerGeneration == 0 ||
			schedulerStableId == 0 ||
			pendingCoreMeshes->GetBaseWorldVoxelRevision() == 0 ||
			pendingCoreMeshes->GetBaseWorldVoxelRevision() !=
			visibleGpuMeshes->GetVisibleWorldRevision() ||
			pendingCoreMeshes->GetTargetWorldVoxelRevision() <=
			pendingCoreMeshes->GetBaseWorldVoxelRevision() ||
			pendingCoreMeshes->GetConfig() != visibleGpuMeshes->GetConfig())
		{
			return false;
		}

		try
		{
			NapaVoxelCpuMeshSet cpuReplacements{};
			if (ConvertNapaVoxelMeshReplacements(pendingCoreMeshes->GetReplacementChunks(),
				pendingCoreMeshes->GetConfig(), cpuReplacements).Failed())
			{
				return false;
			}

			auto prepared = std::make_shared<GGLabMeshPublicationBatch>(
				GGLabMeshPublicationBatch::ConstructionToken{}, std::move(pendingCoreMeshes),
				std::move(cpuReplacements), visibleGpuMeshes, identity, schedulerStableId);
			publication = std::move(prepared);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool GGLabMeshPublicationBatch::PrepareGpuResources(RHIDevice* device) noexcept
	{
		if (m_Status != GGLabMeshPublicationStatus::Uninitialized || !m_PendingCoreMeshes ||
			!m_BaseGpuMeshes || (!device && m_CpuReplacements.m_RenderableChunkCount != 0))
		{
			m_Status = GGLabMeshPublicationStatus::Failed;
			return false;
		}

		try
		{
			m_Replacements.clear();
			m_Replacements.reserve(m_CpuReplacements.m_Chunks.size());
			uint64_t totalBytes = 0;
			uint64_t operationCount = 0;
			for (const NapaVoxelCpuChunkMesh& cpuChunk : m_CpuReplacements.m_Chunks)
			{
				std::shared_ptr<const NapaVoxelGpuChunkMesh> gpuChunk;
				if (!cpuChunk.IsEmpty())
				{
					uint64_t vertexBytes = 0;
					uint64_t indexBytes = 0;
					if (!PrepareGpuChunkMesh(device, m_PendingCoreMeshes->GetConfig(),
						cpuChunk, m_UploadIdentity, "Replacement Chunk", gpuChunk,
						vertexBytes, indexBytes) ||
						!CheckedAccumulate(vertexBytes, totalBytes) ||
						!CheckedAccumulate(indexBytes, totalBytes) ||
						!CheckedAccumulate(2, operationCount))
					{
						m_Status = GGLabMeshPublicationStatus::Failed;
						return false;
					}
				}
				m_Replacements.push_back({
					.m_Chunk = cpuChunk.m_Chunk,
					.m_Mesh = std::move(gpuChunk),
					});
			}

			if (operationCount > std::numeric_limits<uint32_t>::max() ||
				m_Replacements.size() != m_CpuReplacements.m_Chunks.size())
			{
				m_Status = GGLabMeshPublicationStatus::Failed;
				return false;
			}

			auto prospective = std::make_shared<NapaVoxelGpuMeshSet>();
			prospective->m_Config = m_PendingCoreMeshes->GetConfig();
			prospective->m_VisibleWorldRevision = GetTargetWorldRevision();
			prospective->m_Chunks.reserve(
				m_BaseGpuMeshes->m_Chunks.size() + m_Replacements.size());

			size_t baseIndex = 0;
			size_t replacementIndex = 0;
			while (baseIndex < m_BaseGpuMeshes->m_Chunks.size() ||
				replacementIndex < m_Replacements.size())
			{
				const auto& base = baseIndex < m_BaseGpuMeshes->m_Chunks.size()
					? m_BaseGpuMeshes->m_Chunks[baseIndex]
					: std::shared_ptr<const NapaVoxelGpuChunkMesh>{};
				const NapaVoxelGpuChunkReplacement* replacement =
					replacementIndex < m_Replacements.size()
					? &m_Replacements[replacementIndex]
					: nullptr;
				if (!base && baseIndex < m_BaseGpuMeshes->m_Chunks.size())
				{
					m_Status = GGLabMeshPublicationStatus::Failed;
					return false;
				}

				if (base && (!replacement || IsChunkLess(base->m_Chunk, replacement->m_Chunk)))
				{
					prospective->m_Chunks.push_back(base);
					++baseIndex;
				}
				else if (replacement && (!base ||
					IsChunkLess(replacement->m_Chunk, base->m_Chunk)))
				{
					if (replacement->m_Mesh)
					{
						prospective->m_Chunks.push_back(replacement->m_Mesh);
					}
					++replacementIndex;
				}
				else
				{
					if (replacement->m_Mesh)
					{
						prospective->m_Chunks.push_back(replacement->m_Mesh);
					}
					++baseIndex;
					++replacementIndex;
				}
			}

			for (size_t index = 1; index < prospective->m_Chunks.size(); ++index)
			{
				if (!IsChunkLess(prospective->m_Chunks[index - 1]->m_Chunk,
					prospective->m_Chunks[index]->m_Chunk))
				{
					m_Status = GGLabMeshPublicationStatus::Failed;
					return false;
				}
			}

			m_UploadEstimate = {
				.m_SourceBytes = totalBytes,
				.m_StagingBytes = totalBytes,
				.m_OperationCount = static_cast<uint32_t>(operationCount),
			};
			m_ProspectiveGpuMeshes = std::move(prospective);
			m_Status = GGLabMeshPublicationStatus::Prepared;
			return true;
		}
		catch (...)
		{
			m_Status = GGLabMeshPublicationStatus::Failed;
			return false;
		}
	}

	bool GGLabMeshPublicationBatch::MarkQueued() noexcept
	{
		if (m_Status != GGLabMeshPublicationStatus::Prepared || !m_PublicationAllowed)
		{
			return false;
		}
		m_Status = GGLabMeshPublicationStatus::Queued;
		return true;
	}

	bool GGLabMeshPublicationBatch::BeginRecording(
		const AssetStreamingIdentity& identity) noexcept
	{
		if (m_Status != GGLabMeshPublicationStatus::Queued || !m_PublicationAllowed ||
			identity != m_UploadIdentity ||
			m_Identity.m_OwnerGeneration != identity.m_Generation)
		{
			return false;
		}
		m_Status = GGLabMeshPublicationStatus::Recording;
		return true;
	}

	bool GGLabMeshPublicationBatch::RecordUpload(TransferBatch& batch) noexcept
	{
		if (m_Status != GGLabMeshPublicationStatus::Recording ||
			m_Replacements.size() != m_CpuReplacements.m_Chunks.size())
		{
			return false;
		}

		bool succeeded = true;
		for (size_t index = 0; index < m_CpuReplacements.m_Chunks.size(); ++index)
		{
			const NapaVoxelCpuChunkMesh& cpuChunk = m_CpuReplacements.m_Chunks[index];
			const std::shared_ptr<const NapaVoxelGpuChunkMesh>& gpuChunk =
				m_Replacements[index].m_Mesh;
			if (cpuChunk.IsEmpty())
			{
				if (gpuChunk)
				{
					return false;
				}
				continue;
			}
			if (!gpuChunk || gpuChunk->m_Chunk != cpuChunk.m_Chunk)
			{
				return false;
			}

			succeeded &= batch.UploadBuffer(gpuChunk->m_VertexBuffer.Get(), 0,
				cpuChunk.m_Vertices.data(), gpuChunk->m_VertexBufferDesc.m_SizeInBytes);
			succeeded &= batch.UploadBuffer(gpuChunk->m_IndexBuffer.Get(), 0,
				cpuChunk.m_Indices.data(), gpuChunk->m_IndexBufferDesc.m_SizeInBytes);
		}
		return succeeded;
	}

	bool GGLabMeshPublicationBatch::SetUploadHandle(AssetUploadHandle handle) noexcept
	{
		if (m_Status != GGLabMeshPublicationStatus::Recording || !handle.IsValid())
		{
			m_Status = GGLabMeshPublicationStatus::Failed;
			return false;
		}
		m_UploadHandle = handle;
		m_Status = GGLabMeshPublicationStatus::AwaitingFence;
		return true;
	}

	bool GGLabMeshPublicationBatch::CompleteUpload(
		const AssetUploadCompletionInfo& completion) noexcept
	{
		if (completion.m_Identity != m_UploadIdentity || completion.m_Handle != m_UploadHandle ||
			m_HasCompletion)
		{
			return false;
		}

		m_HasCompletion = true;
		m_CompletionFence = completion.m_FencePoint;
		if (m_Status == GGLabMeshPublicationStatus::Cancelled)
		{
			return true;
		}
		if (m_Status != GGLabMeshPublicationStatus::AwaitingFence ||
			completion.m_Status != AssetUploadStatus::Succeeded ||
			!completion.m_FencePoint.IsValid() || !m_PublicationAllowed ||
			m_Identity.m_OwnerGeneration != m_UploadIdentity.m_Generation)
		{
			m_Status = GGLabMeshPublicationStatus::Failed;
			return true;
		}

		m_Status = GGLabMeshPublicationStatus::ReadyForCommit;
		return true;
	}

	void GGLabMeshPublicationBatch::Fail() noexcept
	{
		m_PublicationAllowed = false;
		if (m_Status != GGLabMeshPublicationStatus::Cancelled)
		{
			m_Status = GGLabMeshPublicationStatus::Failed;
		}
	}

	void GGLabMeshPublicationBatch::Cancel() noexcept
	{
		if (m_Status == GGLabMeshPublicationStatus::Cancelled)
		{
			return;
		}
		m_PublicationAllowed = false;
		if (m_Identity.m_OwnerGeneration == std::numeric_limits<uint64_t>::max())
		{
			m_OwnerGenerationExhausted = true;
		}
		else
		{
			++m_Identity.m_OwnerGeneration;
		}
		m_Status = GGLabMeshPublicationStatus::Cancelled;
	}

	uint64_t GGLabMeshPublicationBatch::GetBaseWorldRevision() const noexcept
	{
		return m_BaseWorldRevision;
	}

	uint64_t GGLabMeshPublicationBatch::GetTargetWorldRevision() const noexcept
	{
		return m_TargetWorldRevision;
	}

	void GGLabMeshPublicationBatch::MarkCommitted() noexcept
	{
		m_PublicationAllowed = false;
		m_Status = GGLabMeshPublicationStatus::Committed;
		m_CpuReplacements = {};
		m_BaseGpuMeshes.reset();
		m_ProspectiveGpuMeshes.reset();
		m_Replacements.clear();
	}

	NapaVoxelInitialPublicationOwner::NapaVoxelInitialPublicationOwner(
		ConstructionToken,
		std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pendingCoreMeshes,
		NapaVoxelCpuMeshSet cpuMeshes, uint64_t stableId, uint64_t ownerGeneration) noexcept :
		m_Config(pendingCoreMeshes->GetConfig()),
		m_TargetWorldRevision(pendingCoreMeshes->GetTargetWorldVoxelRevision()),
		m_PendingCoreMeshes(std::move(pendingCoreMeshes)),
		m_CpuMeshes(std::move(cpuMeshes)),
		m_UploadIdentity({
			.m_Kind = AssetStreamingWorkKind::RuntimeMesh,
			.m_StableId = stableId,
			.m_Generation = ownerGeneration,
			}),
			m_OwnerGeneration(ownerGeneration)
	{
	}

	bool PrepareNapaVoxelInitialPublication(
		std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pendingCoreMeshes,
		uint64_t stableId, uint64_t ownerGeneration,
		std::shared_ptr<NapaVoxelInitialPublicationOwner>& publication) noexcept
	{
		if (!IsValidPublicationInput(pendingCoreMeshes.get(), stableId, ownerGeneration))
		{
			return false;
		}

		try
		{
			NapaVoxelCpuMeshSet cpuMeshes{};
			if (ConvertNapaVoxelMeshRecords(pendingCoreMeshes->GetChunks(),
				pendingCoreMeshes->GetConfig(), cpuMeshes).Failed())
			{
				return false;
			}
			auto prepared = std::make_shared<NapaVoxelInitialPublicationOwner>(
				NapaVoxelInitialPublicationOwner::ConstructionToken{},
				std::move(pendingCoreMeshes), std::move(cpuMeshes), stableId, ownerGeneration);
			publication = std::move(prepared);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool NapaVoxelInitialPublicationOwner::PrepareGpuResources(RHIDevice* device) noexcept
	{
		if (m_Status != NapaVoxelInitialPublicationStatus::Uninitialized ||
			(!device && m_CpuMeshes.m_RenderableChunkCount != 0))
		{
			m_Status = NapaVoxelInitialPublicationStatus::Failed;
			return false;
		}

		try
		{
			auto gpuMeshes = std::make_shared<NapaVoxelGpuMeshSet>();
			gpuMeshes->m_Config = m_Config;
			gpuMeshes->m_VisibleWorldRevision = GetTargetWorldRevision();
			gpuMeshes->m_Chunks.reserve(static_cast<size_t>(m_CpuMeshes.m_RenderableChunkCount));

			uint64_t totalBytes = 0;
			uint64_t operationCount = 0;
			for (const NapaVoxelCpuChunkMesh& cpuChunk : m_CpuMeshes.m_Chunks)
			{
				if (cpuChunk.IsEmpty())
				{
					continue;
				}

				uint64_t vertexBytes = 0;
				uint64_t indexBytes = 0;
				std::shared_ptr<const NapaVoxelGpuChunkMesh> gpuChunk;
				if (!PrepareGpuChunkMesh(device, m_Config, cpuChunk, m_UploadIdentity,
					"Static Chunk", gpuChunk, vertexBytes, indexBytes) ||
					!CheckedAccumulate(vertexBytes, totalBytes) ||
					!CheckedAccumulate(indexBytes, totalBytes) ||
					!CheckedAccumulate(2, operationCount))
				{
					m_Status = NapaVoxelInitialPublicationStatus::Failed;
					return false;
				}

				gpuMeshes->m_Chunks.push_back(std::move(gpuChunk));
			}

			if (operationCount > std::numeric_limits<uint32_t>::max() ||
				gpuMeshes->m_Chunks.size() != m_CpuMeshes.m_RenderableChunkCount)
			{
				m_Status = NapaVoxelInitialPublicationStatus::Failed;
				return false;
			}

			m_UploadEstimate = {
				.m_SourceBytes = totalBytes,
				.m_StagingBytes = totalBytes,
				.m_OperationCount = static_cast<uint32_t>(operationCount),
			};
			m_GpuMeshes = std::move(gpuMeshes);
			m_Status = NapaVoxelInitialPublicationStatus::Prepared;
			return true;
		}
		catch (...)
		{
			m_Status = NapaVoxelInitialPublicationStatus::Failed;
			return false;
		}
	}

	bool NapaVoxelInitialPublicationOwner::MarkQueued() noexcept
	{
		if (m_Status != NapaVoxelInitialPublicationStatus::Prepared || !m_PublicationAllowed)
		{
			return false;
		}
		m_Status = NapaVoxelInitialPublicationStatus::Queued;
		return true;
	}

	bool NapaVoxelInitialPublicationOwner::BeginRecording(
		const AssetStreamingIdentity& identity) noexcept
	{
		if (m_Status != NapaVoxelInitialPublicationStatus::Queued || !m_PublicationAllowed ||
			identity != m_UploadIdentity || m_OwnerGeneration != identity.m_Generation)
		{
			return false;
		}
		m_Status = NapaVoxelInitialPublicationStatus::Recording;
		return true;
	}

	bool NapaVoxelInitialPublicationOwner::RecordUpload(TransferBatch& batch) noexcept
	{
		if (m_Status != NapaVoxelInitialPublicationStatus::Recording || !m_GpuMeshes)
		{
			return false;
		}

		bool succeeded = true;
		size_t gpuChunkIndex = 0;
		for (const NapaVoxelCpuChunkMesh& cpuChunk : m_CpuMeshes.m_Chunks)
		{
			if (cpuChunk.IsEmpty())
			{
				continue;
			}
			if (gpuChunkIndex >= m_GpuMeshes->m_Chunks.size())
			{
				return false;
			}

			const auto& gpuChunk = m_GpuMeshes->m_Chunks[gpuChunkIndex++];
			if (!gpuChunk)
			{
				return false;
			}
			succeeded &= batch.UploadBuffer(gpuChunk->m_VertexBuffer.Get(), 0,
				cpuChunk.m_Vertices.data(), gpuChunk->m_VertexBufferDesc.m_SizeInBytes);
			succeeded &= batch.UploadBuffer(gpuChunk->m_IndexBuffer.Get(), 0,
				cpuChunk.m_Indices.data(), gpuChunk->m_IndexBufferDesc.m_SizeInBytes);
		}
		return succeeded && gpuChunkIndex == m_GpuMeshes->m_Chunks.size();
	}

	bool NapaVoxelInitialPublicationOwner::SetUploadHandle(AssetUploadHandle handle) noexcept
	{
		if (m_Status != NapaVoxelInitialPublicationStatus::Recording || !handle.IsValid())
		{
			m_Status = NapaVoxelInitialPublicationStatus::Failed;
			return false;
		}
		m_UploadHandle = handle;
		m_Status = NapaVoxelInitialPublicationStatus::AwaitingFence;
		return true;
	}

	bool NapaVoxelInitialPublicationOwner::CompleteUpload(
		const AssetUploadCompletionInfo& completion) noexcept
	{
		if (completion.m_Identity != m_UploadIdentity || completion.m_Handle != m_UploadHandle ||
			m_Status == NapaVoxelInitialPublicationStatus::Committed || m_HasCompletion)
		{
			return false;
		}

		m_HasCompletion = true;
		m_CompletionFence = completion.m_FencePoint;
		if (m_Status == NapaVoxelInitialPublicationStatus::Cancelled)
		{
			return true;
		}
		if (m_Status != NapaVoxelInitialPublicationStatus::AwaitingFence ||
			completion.m_Status != AssetUploadStatus::Succeeded ||
			!completion.m_FencePoint.IsValid() || !m_PublicationAllowed ||
			m_OwnerGeneration != m_UploadIdentity.m_Generation)
		{
			m_Status = NapaVoxelInitialPublicationStatus::Failed;
			return true;
		}

		m_Status = NapaVoxelInitialPublicationStatus::ReadyForCommit;
		return true;
	}

	void NapaVoxelInitialPublicationOwner::Fail() noexcept
	{
		m_PublicationAllowed = false;
		if (m_Status != NapaVoxelInitialPublicationStatus::Committed &&
			m_Status != NapaVoxelInitialPublicationStatus::Cancelled)
		{
			m_Status = NapaVoxelInitialPublicationStatus::Failed;
		}
	}

	void NapaVoxelInitialPublicationOwner::Cancel() noexcept
	{
		if (m_Status == NapaVoxelInitialPublicationStatus::Committed ||
			m_Status == NapaVoxelInitialPublicationStatus::Cancelled)
		{
			return;
		}

		m_PublicationAllowed = false;
		if (m_OwnerGeneration == std::numeric_limits<uint64_t>::max())
		{
			m_OwnerGenerationExhausted = true;
		}
		else
		{
			++m_OwnerGeneration;
		}
		m_Status = NapaVoxelInitialPublicationStatus::Cancelled;
	}

	uint64_t NapaVoxelInitialPublicationOwner::GetTargetWorldRevision() const noexcept
	{
		return m_TargetWorldRevision;
	}

	void NapaVoxelInitialPublicationOwner::MarkCommitted() noexcept
	{
		m_PublicationAllowed = false;
		m_Status = NapaVoxelInitialPublicationStatus::Committed;
		m_CpuMeshes = {};
		m_GpuMeshes.reset();
	}

	bool NapaVoxelRenderState::PrepareInitialCommit(
		const std::shared_ptr<NapaVoxelInitialPublicationOwner>& publication,
		std::unique_ptr<NapaVoxelPreparedInitialCommit>& preparedCommit) noexcept
	{
		if (!(publication && !m_VisibleCoreMeshes.HasPublishedMeshes() &&
			!m_VisibleGpuMeshes && publication->IsReadyForCommit() &&
			publication->m_PublicationAllowed && !publication->m_OwnerGenerationExhausted &&
			publication->m_GpuMeshes && publication->m_PendingCoreMeshes &&
			publication->GetTargetWorldRevision() != 0 &&
			publication->m_OwnerGeneration == publication->m_UploadIdentity.m_Generation &&
			publication->m_CompletionFence.IsValid() &&
			publication->m_GpuMeshes->GetVisibleWorldRevision() ==
			publication->GetTargetWorldRevision()))
		{
			return false;
		}

		try
		{
			auto prepared = std::make_unique<NapaVoxelPreparedInitialCommit>(
				NapaVoxelPreparedInitialCommit::ConstructionToken{});
			auto damageSnapshot =
				std::make_unique<napa::voxel::VoxelDamageMarkerSnapshot>();
			damageSnapshot->m_SourceWorldVoxelRevision =
				publication->GetTargetWorldRevision();
			prepared->m_GpuMeshes = publication->m_GpuMeshes;
			prepared->m_DamageSnapshot = std::move(damageSnapshot);
			prepared->m_Owner = publication;
			if (napa::voxel::PrepareCpuMeshBatchPublication(
				publication->m_PendingCoreMeshes, m_VisibleCoreMeshes,
				prepared->m_CorePublication).Failed())
			{
				return false;
			}
			preparedCommit = std::move(prepared);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	void NapaVoxelRenderState::CommitInitial(
		std::unique_ptr<NapaVoxelPreparedInitialCommit>& preparedCommit) noexcept
	{
		GGLAB_ASSERT_MSG(preparedCommit && preparedCommit->m_CorePublication &&
			preparedCommit->m_GpuMeshes && preparedCommit->m_DamageSnapshot &&
			preparedCommit->m_Owner,
			"Initial Napa voxel commit requires a prepared CPU/GPU token.");
		if (!preparedCommit || !preparedCommit->m_CorePublication ||
			!preparedCommit->m_GpuMeshes || !preparedCommit->m_DamageSnapshot ||
			!preparedCommit->m_Owner)
		{
			return;
		}

		std::unique_ptr<NapaVoxelPreparedInitialCommit> committed =
			std::move(preparedCommit);
		napa::voxel::CommitCpuMeshBatchPublication(
			committed->m_CorePublication, m_VisibleCoreMeshes);
		GGLAB_ASSERT_MSG(m_VisibleCoreMeshes.HasPublishedMeshes() &&
			m_VisibleCoreMeshes.GetVisibleWorldRevision() ==
			committed->m_GpuMeshes->GetVisibleWorldRevision(),
			"Initial Napa voxel Core and GPU publications must share one revision.");

		m_VisibleGpuMeshes = std::move(committed->m_GpuMeshes);
		m_VisibleDamageSnapshot = std::move(committed->m_DamageSnapshot);
		committed->m_Owner->MarkCommitted();
	}

	bool NapaVoxelRenderState::IsNextIdentity(
		GGLabMeshPublicationIdentity identity) const noexcept
	{
		return identity.m_OperationSerial != 0 && identity.m_PublicationSerial != 0 &&
			identity.m_OwnerGeneration != 0 &&
			identity.m_OperationSerial > m_LastCommittedIdentity.m_OperationSerial &&
			identity.m_PublicationSerial > m_LastCommittedIdentity.m_PublicationSerial &&
			(m_LastCommittedIdentity.m_OwnerGeneration == 0 ||
				identity.m_OwnerGeneration == m_LastCommittedIdentity.m_OwnerGeneration);
	}

	bool NapaVoxelRenderState::PrepareMeshCommit(
		const std::shared_ptr<GGLabMeshPublicationBatch>& publication,
		GGLabMeshPublicationIdentity expectedIdentity,
		std::unique_ptr<napa::voxel::VoxelDamageMarkerSnapshot>& damageSnapshot,
		std::unique_ptr<NapaVoxelPreparedMeshCommit>& preparedCommit) noexcept
	{
		if (!HasVisibleMeshes() || !m_LastSubmittedGraphicsFence.IsValid() ||
			!publication || !publication->IsReadyForCommit() ||
			!publication->m_PublicationAllowed || publication->m_OwnerGenerationExhausted ||
			publication->m_Identity != expectedIdentity || !IsNextIdentity(expectedIdentity) ||
			publication->m_BaseGpuMeshes != m_VisibleGpuMeshes ||
			!publication->m_ProspectiveGpuMeshes || !publication->m_PendingCoreMeshes ||
			publication->m_CompletionFence.IsValid() == false ||
			publication->GetBaseWorldRevision() != GetVisibleWorldRevision() ||
			publication->GetTargetWorldRevision() <= GetVisibleWorldRevision() ||
			publication->m_ProspectiveGpuMeshes->GetVisibleWorldRevision() !=
			publication->GetTargetWorldRevision() ||
			publication->m_ProspectiveGpuMeshes->GetConfig() !=
			m_VisibleCoreMeshes.GetConfig() ||
			!damageSnapshot || damageSnapshot->m_SourceWorldVoxelRevision !=
			publication->GetTargetWorldRevision())
		{
			return false;
		}

		try
		{
			auto prepared = std::make_unique<NapaVoxelPreparedMeshCommit>(
				NapaVoxelPreparedMeshCommit::ConstructionToken{});
			prepared->m_GpuMeshes = publication->m_ProspectiveGpuMeshes;
			prepared->m_Owner = publication;
			prepared->m_Identity = expectedIdentity;
			prepared->m_Retirement = std::make_unique<NapaVoxelRetiredGpuMeshSet>();
			prepared->m_Retirement->m_Meshes = m_VisibleGpuMeshes;
			prepared->m_Retirement->m_LastUseFence = m_LastSubmittedGraphicsFence;
			if (napa::voxel::PrepareCpuMeshBatchPublication(
				publication->m_PendingCoreMeshes, m_VisibleCoreMeshes,
				prepared->m_CorePublication).Failed())
			{
				return false;
			}
			prepared->m_DamageSnapshot = std::move(damageSnapshot);
			preparedCommit = std::move(prepared);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	void NapaVoxelRenderState::CommitMesh(
		std::unique_ptr<NapaVoxelPreparedMeshCommit>& preparedCommit) noexcept
	{
		GGLAB_ASSERT_MSG(preparedCommit && preparedCommit->m_CorePublication &&
			preparedCommit->m_GpuMeshes && preparedCommit->m_DamageSnapshot &&
			preparedCommit->m_Owner && preparedCommit->m_Retirement,
			"Napa voxel mesh commit requires one complete prepared token.");

		std::unique_ptr<NapaVoxelPreparedMeshCommit> committed =
			std::move(preparedCommit);
		napa::voxel::CommitCpuMeshBatchPublication(
			committed->m_CorePublication, m_VisibleCoreMeshes);
		m_VisibleGpuMeshes = std::move(committed->m_GpuMeshes);
		m_VisibleDamageSnapshot = std::move(committed->m_DamageSnapshot);
		committed->m_Retirement->m_Next = std::move(m_RetiredGpuMeshes);
		m_RetiredGpuMeshes = std::move(committed->m_Retirement);
		m_LastCommittedIdentity = committed->m_Identity;
		committed->m_Owner->MarkCommitted();
	}

	bool NapaVoxelRenderState::PrepareDataOnlyCommit(
		const napa::voxel::VoxelWorld& authoritativeWorld,
		const napa::voxel::VoxelMutationResult& mutation,
		GGLabMeshPublicationIdentity identity,
		std::unique_ptr<napa::voxel::VoxelDamageMarkerSnapshot>& damageSnapshot,
		std::unique_ptr<NapaVoxelPreparedDataOnlyCommit>& preparedCommit) noexcept
	{
		if (!HasVisibleMeshes() || !m_LastSubmittedGraphicsFence.IsValid() ||
			!IsNextIdentity(identity) || !damageSnapshot ||
			damageSnapshot->m_SourceWorldVoxelRevision !=
			mutation.m_TargetWorldVoxelRevision)
		{
			return false;
		}

		try
		{
			auto prepared = std::make_unique<NapaVoxelPreparedDataOnlyCommit>(
				NapaVoxelPreparedDataOnlyCommit::ConstructionToken{});
			auto prospective = std::make_shared<NapaVoxelGpuMeshSet>();
			prospective->m_Config = m_VisibleGpuMeshes->m_Config;
			prospective->m_VisibleWorldRevision = mutation.m_TargetWorldVoxelRevision;
			prospective->m_Chunks = m_VisibleGpuMeshes->m_Chunks;
			prepared->m_GpuMeshes = std::move(prospective);
			prepared->m_Identity = identity;
			prepared->m_Retirement = std::make_unique<NapaVoxelRetiredGpuMeshSet>();
			prepared->m_Retirement->m_Meshes = m_VisibleGpuMeshes;
			prepared->m_Retirement->m_LastUseFence = m_LastSubmittedGraphicsFence;
			if (napa::voxel::PrepareDataOnlyPublication(
				authoritativeWorld, mutation, m_VisibleCoreMeshes,
				prepared->m_CorePublication).Failed())
			{
				return false;
			}
			prepared->m_DamageSnapshot = std::move(damageSnapshot);
			preparedCommit = std::move(prepared);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	void NapaVoxelRenderState::CommitDataOnly(
		std::unique_ptr<NapaVoxelPreparedDataOnlyCommit>& preparedCommit) noexcept
	{
		GGLAB_ASSERT_MSG(preparedCommit && preparedCommit->m_CorePublication &&
			preparedCommit->m_GpuMeshes && preparedCommit->m_DamageSnapshot &&
			preparedCommit->m_Retirement,
			"Napa voxel data-only commit requires one complete prepared token.");

		std::unique_ptr<NapaVoxelPreparedDataOnlyCommit> committed =
			std::move(preparedCommit);
		napa::voxel::CommitDataOnlyPublication(
			committed->m_CorePublication, m_VisibleCoreMeshes);
		m_VisibleGpuMeshes = std::move(committed->m_GpuMeshes);
		m_VisibleDamageSnapshot = std::move(committed->m_DamageSnapshot);
		committed->m_Retirement->m_Next = std::move(m_RetiredGpuMeshes);
		m_RetiredGpuMeshes = std::move(committed->m_Retirement);
		m_LastCommittedIdentity = committed->m_Identity;
	}

	void NapaVoxelRenderState::OnFrameSubmitted(RHIFencePoint fencePoint) noexcept
	{
		if (!fencePoint.IsValid())
		{
			return;
		}
		if (m_LastSubmittedGraphicsFence.IsValid() &&
			m_LastSubmittedGraphicsFence.m_Fence == fencePoint.m_Fence &&
			fencePoint.m_Value <= m_LastSubmittedGraphicsFence.m_Value)
		{
			return;
		}
		m_LastSubmittedGraphicsFence = fencePoint;
	}

	void NapaVoxelRenderState::RetireCompletedGpuMeshes(RHIDevice* device) noexcept
	{
		if (!device)
		{
			return;
		}
		auto* retirement = &m_RetiredGpuMeshes;
		while (*retirement)
		{
			if (device->IsFencePointCompleted((*retirement)->m_LastUseFence))
			{
				*retirement = std::move((*retirement)->m_Next);
			}
			else
			{
				retirement = &(*retirement)->m_Next;
			}
		}
	}

	size_t NapaVoxelRenderState::GetRetiredGpuMeshSetCount() const noexcept
	{
		size_t count = 0;
		for (const NapaVoxelRetiredGpuMeshSet* retirement = m_RetiredGpuMeshes.get();
			retirement; retirement = retirement->m_Next.get())
		{
			++count;
		}
		return count;
	}

	void NapaVoxelRenderState::Reset() noexcept
	{
		m_LastSubmittedGraphicsFence.Reset();
		m_LastCommittedIdentity = {};
		m_RetiredGpuMeshes.reset();
		m_VisibleDamageSnapshot.reset();
		m_VisibleGpuMeshes.reset();
		m_VisibleCoreMeshes = {};
	}

	NapaVoxelMeshReplacementUploadSession::NapaVoxelMeshReplacementUploadSession(
		RHIDevice* device, AssetUploadScheduler* scheduler,
		NapaVoxelCommandQueue* commandQueue,
		NapaVoxelPublicationSerialState serialState) noexcept :
		m_Device(device), m_Scheduler(scheduler), m_CommandQueue(commandQueue),
		m_LastPublicationSerial(serialState.m_LastPublicationSerial)
	{
	}

	NapaVoxelMeshReplacementUploadSession::~NapaVoxelMeshReplacementUploadSession()
	{
		CancelPrepare();
	}

	bool NapaVoxelMeshReplacementUploadSession::BeginPrepare(
		std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pendingCoreMeshes,
		const std::shared_ptr<const NapaVoxelGpuMeshSet>& visibleGpuMeshes,
		uint64_t operationSerial, uint64_t ownerGeneration) noexcept
	{
		CancelPrepare();
		m_HasFailed = false;
		if (m_LastPublicationSerial == std::numeric_limits<uint64_t>::max())
		{
			FailHostPreparation(true);
			return false;
		}

		const uint64_t publicationSerial = m_LastPublicationSerial + 1;
		uint64_t stableId = 0;
		if (!m_Device || !m_Scheduler || !m_CommandQueue ||
			!AllocateNapaVoxelPublicationStableId(stableId) ||
			!PrepareGGLabMeshPublicationBatch(pendingCoreMeshes, visibleGpuMeshes, {
				.m_OperationSerial = operationSerial,
				.m_PublicationSerial = publicationSerial,
				.m_OwnerGeneration = ownerGeneration,
				}, stableId, m_Publication))
		{
			FailHostPreparation(false);
			return false;
		}

		m_LastPublicationSerial = publicationSerial;
		if (!m_Publication->PrepareGpuResources(m_Device) || !m_Publication->MarkQueued())
		{
			FailHostPreparation(false);
			return false;
		}

		ScheduleUpload();
		return !m_HasFailed;
	}

	void NapaVoxelMeshReplacementUploadSession::TickPrepare() noexcept
	{
		if (!m_Publication || m_IsReady || m_HasFailed)
		{
			return;
		}

		switch (m_Publication->GetStatus())
		{
		case GGLabMeshPublicationStatus::ReadyForCommit:
			m_IsReady = true;
			break;
		case GGLabMeshPublicationStatus::Committed:
			break;
		case GGLabMeshPublicationStatus::Failed:
			FailHostPreparation(false);
			break;
		case GGLabMeshPublicationStatus::Cancelled:
		case GGLabMeshPublicationStatus::Uninitialized:
		case GGLabMeshPublicationStatus::Prepared:
		case GGLabMeshPublicationStatus::Queued:
		case GGLabMeshPublicationStatus::Recording:
		case GGLabMeshPublicationStatus::AwaitingFence:
			break;
		}
	}

	void NapaVoxelMeshReplacementUploadSession::CancelPrepare() noexcept
	{
		if (m_Publication)
		{
			const AssetStreamingIdentity identity = m_Publication->GetUploadIdentity();
			m_Publication->Cancel();
			if (m_Scheduler)
			{
				GGLAB_UNUSED(m_Scheduler->CancelReadyWork(identity));
			}
			m_Publication.reset();
		}
		m_IsReady = false;
		m_HasFailed = false;
	}

	void NapaVoxelMeshReplacementUploadSession::ScheduleUpload() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_Scheduler);
		GGLAB_ASSERT_NOT_NULL(m_Publication.get());
		if (!m_Scheduler || !m_Publication)
		{
			FailHostPreparation(false);
			return;
		}

		const std::shared_ptr<GGLabMeshPublicationBatch> publication = m_Publication;
		const AssetStreamingIdentity identity = publication->GetUploadIdentity();
		const AssetStreamingWorkEstimate estimate = publication->GetUploadEstimate();
		AssetUploadScheduler* scheduler = m_Scheduler;
		scheduler->EnqueueUploadRecording({
			.m_Name = "Napa Voxel Mesh Replacement Upload",
			.m_Identity = identity,
			.m_Estimate = estimate,
			.m_Priority = TaskPriority::High,
			},
			[scheduler, publication, identity, estimate]() noexcept
			{
				if (!publication->BeginRecording(identity))
				{
					return;
				}
				const AssetUploadHandle handle = scheduler->RecordUpload({
					.m_Name = "Napa Voxel Mesh Replacement Publication",
					.m_Identity = identity,
					.m_Estimate = estimate,
					.m_Priority = TaskPriority::High,
					},
					[publication](TransferBatch& batch) noexcept
					{ return publication->RecordUpload(batch); },
					[publication](const AssetUploadCompletionInfo& completion) noexcept
					{ GGLAB_UNUSED(publication->CompleteUpload(completion)); });
				GGLAB_UNUSED(publication->SetUploadHandle(handle));
			});
	}

	void NapaVoxelMeshReplacementUploadSession::FailHostPreparation(
		bool publicationSerialExhausted) noexcept
	{
		if (m_Publication)
		{
			m_Publication->Fail();
		}
		m_IsReady = false;
		m_HasFailed = true;
		if (m_CommandQueue)
		{
			m_CommandQueue->Freeze(publicationSerialExhausted
				? NapaVoxelCommandQueueError::PublicationSerialExhausted
				: NapaVoxelCommandQueueError::HostPreparationFailed);
		}
	}

	NapaVoxelStaticPublicationSession::NapaVoxelStaticPublicationSession(
		RHIDevice* device, AssetUploadScheduler* scheduler) noexcept :
		m_Device(device), m_Scheduler(scheduler)
	{
	}

	NapaVoxelStaticPublicationSession::~NapaVoxelStaticPublicationSession()
	{
		CancelPrepare();
	}

	bool NapaVoxelStaticPublicationSession::BeginPrepare(
		std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pendingCoreMeshes,
		uint64_t stableId, uint64_t ownerGeneration) noexcept
	{
		CancelPrepare();
		m_HasFailed = true;
		if (!m_Device || !m_Scheduler ||
			!PrepareNapaVoxelInitialPublication(
				pendingCoreMeshes, stableId, ownerGeneration, m_Publication) ||
			!m_Publication->PrepareGpuResources(m_Device) || !m_Publication->MarkQueued())
		{
			if (m_Publication)
			{
				m_Publication->Cancel();
				m_Publication.reset();
			}
			return false;
		}

		m_HasFailed = false;
		ScheduleUpload();
		return !m_HasFailed;
	}

	void NapaVoxelStaticPublicationSession::TickPrepare() noexcept
	{
		if (!m_Publication || m_IsReady || m_HasFailed)
		{
			return;
		}

		switch (m_Publication->GetStatus())
		{
		case NapaVoxelInitialPublicationStatus::ReadyForCommit:
		{
			std::unique_ptr<NapaVoxelPreparedInitialCommit> preparedCommit;
			if (!m_RenderState.PrepareInitialCommit(m_Publication, preparedCommit))
			{
				m_HasFailed = true;
				return;
			}
			m_RenderState.CommitInitial(preparedCommit);
			m_FrameView = m_RenderState.GetVisibleGpuMeshes();
			m_IsReady = m_RenderState.HasVisibleMeshes() && m_FrameView &&
				m_RenderState.GetVisibleWorldRevision() ==
				m_Publication->GetTargetWorldRevision();
			m_HasFailed = !m_IsReady;
		}
		break;
		case NapaVoxelInitialPublicationStatus::Failed:
		case NapaVoxelInitialPublicationStatus::Cancelled:
			m_HasFailed = true;
			break;
		case NapaVoxelInitialPublicationStatus::Uninitialized:
		case NapaVoxelInitialPublicationStatus::Prepared:
		case NapaVoxelInitialPublicationStatus::Queued:
		case NapaVoxelInitialPublicationStatus::Recording:
		case NapaVoxelInitialPublicationStatus::AwaitingFence:
		case NapaVoxelInitialPublicationStatus::Committed:
			break;
		}
	}

	void NapaVoxelStaticPublicationSession::CancelPrepare() noexcept
	{
		if (m_Publication)
		{
			const AssetStreamingIdentity identity = m_Publication->GetUploadIdentity();
			m_Publication->Cancel();
			if (m_Scheduler)
			{
				GGLAB_UNUSED(m_Scheduler->CancelReadyWork(identity));
			}
			m_Publication.reset();
		}
		m_FrameView.reset();
		m_RenderState.Reset();
		m_IsReady = false;
		m_HasFailed = false;
	}

	void NapaVoxelStaticPublicationSession::OnFrameSubmitted(
		RHIFencePoint fencePoint) noexcept
	{
		m_RenderState.OnFrameSubmitted(fencePoint);
	}

	void NapaVoxelStaticPublicationSession::RetireCompletedGpuMeshes() noexcept
	{
		m_RenderState.RetireCompletedGpuMeshes(m_Device);
	}

	void NapaVoxelStaticPublicationSession::ScheduleUpload() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_Scheduler);
		GGLAB_ASSERT_NOT_NULL(m_Publication.get());
		if (!m_Scheduler || !m_Publication)
		{
			m_HasFailed = true;
			return;
		}

		const std::shared_ptr<NapaVoxelInitialPublicationOwner> publication = m_Publication;
		const AssetStreamingIdentity identity = publication->GetUploadIdentity();
		const AssetStreamingWorkEstimate estimate = publication->GetUploadEstimate();
		AssetUploadScheduler* scheduler = m_Scheduler;
		scheduler->EnqueueUploadRecording({
			.m_Name = "Napa Voxel Initial Mesh Upload",
			.m_Identity = identity,
			.m_Estimate = estimate,
			.m_Priority = TaskPriority::High,
			},
			[scheduler, publication, identity, estimate]() noexcept
			{
				if (!publication->BeginRecording(identity))
				{
					return;
				}
				const AssetUploadHandle handle = scheduler->RecordUpload({
					.m_Name = "Napa Voxel Initial Mesh Publication",
					.m_Identity = identity,
					.m_Estimate = estimate,
					.m_Priority = TaskPriority::High,
					},
					[publication](TransferBatch& batch) noexcept
					{ return publication->RecordUpload(batch); },
					[publication](const AssetUploadCompletionInfo& completion) noexcept
					{ GGLAB_UNUSED(publication->CompleteUpload(completion)); });
				GGLAB_UNUSED(publication->SetUploadHandle(handle));
			});
	}

	bool AllocateNapaVoxelPublicationStableId(uint64_t& stableId) noexcept
	{
		uint64_t current = NextNapaVoxelPublicationStableId.load(std::memory_order_relaxed);
		for (;;)
		{
			if (current == 0)
			{
				return false;
			}
			const uint64_t next = current == std::numeric_limits<uint64_t>::max()
				? 0
				: current + 1;
			if (NextNapaVoxelPublicationStableId.compare_exchange_weak(
				current, next, std::memory_order_relaxed, std::memory_order_relaxed))
			{
				stableId = current;
				return true;
			}
		}
	}
}
