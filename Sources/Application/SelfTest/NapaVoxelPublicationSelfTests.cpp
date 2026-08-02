#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "Application/Lab/NapaVoxel/NapaVoxelRenderState.h"

#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"

#include <array>
#include <limits>
#include <memory>

namespace gglab
{
	namespace
	{
		[[nodiscard]] napa::voxel::VoxelWorldConfig MakePublicationConfig() noexcept
		{
			return {
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 1.0f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = {},
					.m_MaxExclusive = { 8, 8, 8 },
					},
			};
		}

		[[nodiscard]] bool BuildPublicationOwner(bool renderable, uint64_t stableId,
			uint64_t ownerGeneration,
			std::shared_ptr<NapaVoxelInitialPublicationOwner>& publication) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config = MakePublicationConfig();
			const PrimitiveDesc sphere{
				.m_StableId = { 1 },
				.m_Material = VoxelMaterial::Soil,
				.m_Shape = PrimitiveShape::Sphere,
				.m_Parameters = {
					.m_Sphere = {
						.m_Center = { 4.0, 4.0, 4.0 },
						.m_Radius = 2.0,
						},
					},
			};
			const std::span<const PrimitiveDesc> primitives = renderable
				? std::span<const PrimitiveDesc>(&sphere, 1)
				: std::span<const PrimitiveDesc>{};
			std::unique_ptr<VoxelWorld> world;
			PrimitiveWorldGenerationResult generation{};
			if (GeneratePrimitiveVoxelWorld(config, primitives, world, generation).Failed() || !world)
			{
				return false;
			}

			constexpr std::array chunks{ ChunkCoord{} };
			CpuMeshBatch batch{};
			VisibleMeshSet visible{};
			std::unique_ptr<PendingCpuMeshBatch> pending;
			if (BuildCpuMeshBatch(*world, world->GetWorldVoxelRevision(), chunks, batch).Failed() ||
				ValidateCpuMeshBatch(batch, visible, pending).Failed() || !pending)
			{
				return false;
			}

			NapaVoxelCpuMeshSet cpuMeshes{};
			if (ConvertNapaVoxelMeshRecords(pending->GetChunks(), config, cpuMeshes).Failed())
			{
				return false;
			}
			return NapaVoxelInitialPublicationOwner::Create(config, std::move(pending),
				std::move(cpuMeshes), stableId, ownerGeneration, publication);
		}

		[[nodiscard]] AssetUploadCompletionInfo MakeCompletion(
			const NapaVoxelInitialPublicationOwner& publication, AssetUploadStatus status,
			RHIFencePoint fence = { RHIFenceHandle{ 1, 1 }, 7 }) noexcept
		{
			return {
				.m_Handle = publication.GetUploadHandle(),
				.m_Identity = publication.GetUploadIdentity(),
				.m_Status = status,
				.m_FencePoint = fence,
			};
		}

		[[nodiscard]] bool AdvanceToAwaitingFence(
			NapaVoxelInitialPublicationOwner& publication, AssetUploadHandle handle) noexcept
		{
			return publication.PrepareGpuResources(nullptr) && publication.MarkQueued() &&
				publication.BeginRecording(publication.GetUploadIdentity()) &&
				publication.SetUploadHandle(handle);
		}

		void RunInitialPublicationStateTests(SelfTestContext& context) noexcept
		{
			std::shared_ptr<NapaVoxelInitialPublicationOwner> publication;
			const bool built = BuildPublicationOwner(false, 1001, 1, publication);
			context.Check(built && publication && publication->GetTargetWorldRevision() == 1 &&
				publication->GetUploadIdentity().m_Kind == AssetStreamingWorkKind::RuntimeMesh &&
				ToAssetKind(publication->GetUploadIdentity().m_Kind) == AssetKind::Unknown,
				"Initial voxel publications use an isolated runtime-mesh Scheduler identity");
			if (!built)
			{
				return;
			}

			NapaVoxelRenderState renderState{};
			const bool awaiting = AdvanceToAwaitingFence(*publication, { 31 });
			context.Check(awaiting && !renderState.HasVisibleMeshes() &&
				renderState.GetVisibleWorldRevision() == 0,
				"Initial CPU and GPU state remains uninitialized before Copy Fence completion");
			const bool completed = publication->CompleteUpload(
				MakeCompletion(*publication, AssetUploadStatus::Succeeded));
			context.Check(completed && publication->IsReadyForCommit() &&
				!renderState.HasVisibleMeshes(),
				"Scheduler completion only marks the durable publication ready for commit");
			const bool prepared = renderState.PrepareInitialCommit(publication);
			if (prepared)
			{
				renderState.CommitInitial(publication);
			}
			context.Check(prepared && renderState.HasVisibleMeshes() &&
				renderState.GetVisibleWorldRevision() == 1 &&
				publication->GetStatus() == NapaVoxelInitialPublicationStatus::Committed,
				"Owner-thread commit publishes Core and GPU initial state at one revision");

			std::shared_ptr<NapaVoxelInitialPublicationOwner> stalePublication;
			const bool staleReady = BuildPublicationOwner(false, 1006, 1, stalePublication) &&
				stalePublication && AdvanceToAwaitingFence(*stalePublication, { 34 }) &&
				stalePublication->CompleteUpload(
					MakeCompletion(*stalePublication, AssetUploadStatus::Succeeded));
			context.Check(staleReady &&
				!renderState.PrepareInitialCommit(stalePublication) &&
				renderState.GetVisibleWorldRevision() == 1,
				"Host revalidation rejects a same-revision initial publication before commit");
		}

		void RunInitialPublicationFailureTests(SelfTestContext& context) noexcept
		{
			std::shared_ptr<NapaVoxelInitialPublicationOwner> createFailure;
			NapaVoxelRenderState createFailureState{};
			const bool builtRenderable = BuildPublicationOwner(true, 1002, 1, createFailure);
			context.Check(builtRenderable && createFailure &&
				!createFailure->PrepareGpuResources(nullptr) &&
				createFailure->GetStatus() == NapaVoxelInitialPublicationStatus::Failed &&
				!createFailureState.HasVisibleMeshes(),
				"GPU resource creation failure preserves an uninitialized visible state");

			std::shared_ptr<NapaVoxelInitialPublicationOwner> uploadFailure;
			NapaVoxelRenderState uploadFailureState{};
			const bool builtUploadFailure = BuildPublicationOwner(false, 1003, 1, uploadFailure);
			const bool awaiting = builtUploadFailure &&
				AdvanceToAwaitingFence(*uploadFailure, { 32 });
			const bool failed = awaiting && uploadFailure->CompleteUpload(
				MakeCompletion(*uploadFailure, AssetUploadStatus::Failed));
			context.Check(failed &&
				uploadFailure->GetStatus() == NapaVoxelInitialPublicationStatus::Failed &&
				!uploadFailureState.PrepareInitialCommit(uploadFailure) &&
				!uploadFailureState.HasVisibleMeshes(),
				"Upload recording or completion failure cannot publish partial CPU/GPU state");
		}

		void RunInitialPublicationCancellationTests(SelfTestContext& context) noexcept
		{
			std::shared_ptr<NapaVoxelInitialPublicationOwner> queued;
			const bool builtQueued = BuildPublicationOwner(false, 1004, 1, queued);
			const bool queuedForRecording = builtQueued && queued &&
				queued->PrepareGpuResources(nullptr) &&
				queued->MarkQueued();
			const AssetStreamingIdentity queuedIdentity = queued
				? queued->GetUploadIdentity()
				: AssetStreamingIdentity{};
			if (queued)
			{
				queued->Cancel();
			}
			context.Check(queuedForRecording &&
				queued->GetStatus() == NapaVoxelInitialPublicationStatus::Cancelled &&
				queued->GetOwnerGeneration() == 2 &&
				!queued->BeginRecording(queuedIdentity),
				"Recording-before cancellation invalidates queued initial upload work");

			std::shared_ptr<NapaVoxelInitialPublicationOwner> inFlight;
			const bool builtInFlight = BuildPublicationOwner(false, 1005, 7, inFlight);
			const bool awaiting = builtInFlight && inFlight &&
				AdvanceToAwaitingFence(*inFlight, { 33 });
			std::weak_ptr<NapaVoxelInitialPublicationOwner> weakOwner = inFlight;
			std::shared_ptr<NapaVoxelInitialPublicationOwner> durableCompletionOwner = inFlight;
			if (!durableCompletionOwner)
			{
				context.Check(false,
					"Recording-after cancellation keeps a durable owner through Copy Fence retirement");
				context.Check(false,
					"Cancelled publication resources retire after the durable completion owner releases");
				return;
			}
			inFlight->Cancel();
			inFlight.reset();
			const bool completionAccepted = durableCompletionOwner->CompleteUpload(
				MakeCompletion(*durableCompletionOwner, AssetUploadStatus::Succeeded));
			context.Check(awaiting && completionAccepted && !weakOwner.expired() &&
				durableCompletionOwner->GetStatus() ==
					NapaVoxelInitialPublicationStatus::Cancelled &&
				durableCompletionOwner->HasCompletion(),
				"Recording-after cancellation keeps a durable owner through Copy Fence retirement");
			durableCompletionOwner.reset();
			context.Check(weakOwner.expired(),
				"Cancelled publication resources retire after the durable completion owner releases");
		}

		void RunInitialPublicationIdentityTests(SelfTestContext& context) noexcept
		{
			std::shared_ptr<NapaVoxelInitialPublicationOwner> first;
			std::shared_ptr<NapaVoxelInitialPublicationOwner> second;
			const bool built = BuildPublicationOwner(false, 2001, 1, first) &&
				BuildPublicationOwner(false, 2002, 1, second) &&
				first && second &&
				AdvanceToAwaitingFence(*first, { 41 }) &&
				AdvanceToAwaitingFence(*second, { 42 });
			const RHIFencePoint sharedBatchFence{ RHIFenceHandle{ 2, 1 }, 17 };
			const bool completed = built && first->CompleteUpload(
				MakeCompletion(*first, AssetUploadStatus::Succeeded, sharedBatchFence)) &&
				second->CompleteUpload(
					MakeCompletion(*second, AssetUploadStatus::Succeeded, sharedBatchFence));
			context.Check(completed && first->GetUploadIdentity() != second->GetUploadIdentity() &&
				first->GetUploadHandle() != second->GetUploadHandle() &&
				first->GetCompletionFence() == second->GetCompletionFence() &&
				first->IsReadyForCommit() && second->IsReadyForCommit(),
				"Scheduler co-batching preserves each voxel publication handle and identity");

			std::shared_ptr<NapaVoxelInitialPublicationOwner> exhausted;
			const bool builtExhausted = BuildPublicationOwner(false, 2003,
				std::numeric_limits<uint64_t>::max(), exhausted);
			const AssetStreamingIdentity oldIdentity = exhausted
				? exhausted->GetUploadIdentity()
				: AssetStreamingIdentity{};
			if (exhausted)
			{
				exhausted->Cancel();
			}
			context.Check(builtExhausted && exhausted->IsOwnerGenerationExhausted() &&
				exhausted->GetOwnerGeneration() == std::numeric_limits<uint64_t>::max() &&
				!exhausted->BeginRecording(oldIdentity),
				"Owner generation exhaustion invalidates the old token without wrapping");
		}
	}

	void RunNapaVoxelPublicationSelfTests(SelfTestContext& context) noexcept
	{
		RunInitialPublicationStateTests(context);
		RunInitialPublicationFailureTests(context);
		RunInitialPublicationCancellationTests(context);
		RunInitialPublicationIdentityTests(context);
	}
}
