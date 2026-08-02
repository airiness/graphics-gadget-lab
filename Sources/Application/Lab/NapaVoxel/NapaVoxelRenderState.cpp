#include "Core/Precompiled.h"
#include "Application/Lab/NapaVoxel/NapaVoxelRenderState.h"

#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/TransferBatch.h"

#include <atomic>
#include <limits>

namespace gglab
{
	namespace
	{
		std::atomic<uint64_t> NextNapaVoxelPublicationStableId = 1;

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

				const uint64_t vertexBytes =
					static_cast<uint64_t>(cpuChunk.m_Vertices.size()) * sizeof(NapaVoxelRenderVertex);
				const uint64_t indexBytes =
					static_cast<uint64_t>(cpuChunk.m_Indices.size()) * sizeof(uint32_t);
				if (vertexBytes == 0 || indexBytes == 0 ||
					vertexBytes > std::numeric_limits<uint32_t>::max() ||
					indexBytes > std::numeric_limits<uint32_t>::max() ||
					!CheckedAccumulate(vertexBytes, totalBytes) ||
					!CheckedAccumulate(indexBytes, totalBytes) ||
					!CheckedAccumulate(2, operationCount))
				{
					m_Status = NapaVoxelInitialPublicationStatus::Failed;
					return false;
				}

				Vector3 translation{};
				if (ComputeNapaVoxelRenderTranslation(
					m_Config, cpuChunk.m_ChunkOrigin, {}, translation).Failed())
				{
					m_Status = NapaVoxelInitialPublicationStatus::Failed;
					return false;
				}

				NapaVoxelGpuChunkMesh gpuChunk{};
				gpuChunk.m_Chunk = cpuChunk.m_Chunk;
				gpuChunk.m_Translation = translation;
				gpuChunk.m_VertexBufferDesc = {
					.m_SizeInBytes = vertexBytes,
					.m_StrideInBytes = sizeof(NapaVoxelRenderVertex),
					.m_Usage = RHIBufferUsage::Vertex | RHIBufferUsage::CopyDest,
					.m_DebugName = "NapaVoxel.VertexBuffer",
				};
				gpuChunk.m_IndexBufferDesc = {
					.m_SizeInBytes = indexBytes,
					.m_StrideInBytes = sizeof(uint32_t),
					.m_Usage = RHIBufferUsage::Index | RHIBufferUsage::CopyDest,
					.m_DebugName = "NapaVoxel.IndexBuffer",
				};

				const RHIResourceDebugIdentityDesc vertexIdentity{
					.m_Domain = RHIResourceDebugDomain::Renderer,
					.m_Category = "NapaVoxel.VertexBuffer",
					.m_Label = "Static Chunk",
					.m_StableId = m_UploadIdentity.m_StableId,
				};
				const RHIResourceDebugIdentityDesc indexIdentity{
					.m_Domain = RHIResourceDebugDomain::Renderer,
					.m_Category = "NapaVoxel.IndexBuffer",
					.m_Label = "Static Chunk",
					.m_StableId = m_UploadIdentity.m_StableId,
				};
				gpuChunk.m_VertexBuffer = RHIBufferOwner(
					device, device->CreateBuffer(gpuChunk.m_VertexBufferDesc, vertexIdentity));
				gpuChunk.m_IndexBuffer = RHIBufferOwner(
					device, device->CreateBuffer(gpuChunk.m_IndexBufferDesc, indexIdentity));
				if (!gpuChunk.m_VertexBuffer || !gpuChunk.m_IndexBuffer)
				{
					m_Status = NapaVoxelInitialPublicationStatus::Failed;
					return false;
				}

				gpuChunk.m_Sections.reserve(cpuChunk.m_SectionDrawRanges.size());
				for (const NapaVoxelSectionDrawRange& section : cpuChunk.m_SectionDrawRanges)
				{
					gpuChunk.m_Sections.push_back({
						.m_Material = section.m_Material,
						.m_FirstIndex = section.m_FirstIndex,
						.m_IndexCount = section.m_IndexCount,
						});
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

			const NapaVoxelGpuChunkMesh& gpuChunk = m_GpuMeshes->m_Chunks[gpuChunkIndex++];
			succeeded &= batch.UploadBuffer(gpuChunk.m_VertexBuffer.Get(), 0,
				cpuChunk.m_Vertices.data(), gpuChunk.m_VertexBufferDesc.m_SizeInBytes);
			succeeded &= batch.UploadBuffer(gpuChunk.m_IndexBuffer.Get(), 0,
				cpuChunk.m_Indices.data(), gpuChunk.m_IndexBufferDesc.m_SizeInBytes);
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
			if (napa::voxel::PrepareCpuMeshBatchPublication(
				publication->m_PendingCoreMeshes, m_VisibleCoreMeshes,
				prepared->m_CorePublication).Failed())
			{
				return false;
			}
			prepared->m_GpuMeshes = publication->m_GpuMeshes;
			prepared->m_Owner = publication;
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
			preparedCommit->m_GpuMeshes && preparedCommit->m_Owner,
			"Initial Napa voxel commit requires a prepared CPU/GPU token.");
		if (!preparedCommit || !preparedCommit->m_CorePublication ||
			!preparedCommit->m_GpuMeshes || !preparedCommit->m_Owner)
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
		committed->m_Owner->MarkCommitted();
	}

	void NapaVoxelRenderState::Reset() noexcept
	{
		m_VisibleGpuMeshes.reset();
		m_VisibleCoreMeshes = {};
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
