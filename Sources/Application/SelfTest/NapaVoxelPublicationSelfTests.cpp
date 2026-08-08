#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "Application/Lab/NapaVoxel/NapaVoxelCommands.h"
#include "Application/Lab/NapaVoxel/NapaVoxelRenderState.h"

#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/RHI/RHITransferContext.h"
#include "Graphics/TransferManager.h"

#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Edit/VoxelMutation.h"
#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"

#include <array>
#include <limits>
#include <memory>
#include <unordered_set>

namespace gglab
{
	namespace
	{
		class NapaVoxelPublicationTestDevice final : public RHIDevice
		{
		public:
			RHIBackendType GetBackendType() const noexcept override { return {}; }
			std::string_view GetAdapterCompatibilityIdentity() const noexcept override
			{
				return "NapaVoxel.PublicationTestDevice";
			}
			RHIShaderWaveCapabilities GetShaderWaveCapabilities() const noexcept override
			{
				return {};
			}
			RHITextureSupportResult QueryTextureSupport(
				const RHITextureDesc&) const noexcept override
			{
				return {};
			}
			RHITextureSupportResult QueryTextureViewSupport(
				const RHITextureDesc&, const RHITextureViewDesc&) const noexcept override
			{
				return {};
			}
			RHITextureHandle CreateTexture(
				const RHITextureDesc&, const RHIResourceDebugIdentityDesc&) noexcept override
			{
				return {};
			}
			RHIBufferHandle CreateBuffer(
				const RHIBufferDesc&, const RHIResourceDebugIdentityDesc&) noexcept override
			{
				if (m_FailNextBufferCreation)
				{
					m_FailNextBufferCreation = false;
					return {};
				}
				const RHIBufferHandle handle{ m_NextBufferIndex++, 1 };
				m_LiveBuffers.insert(handle);
				++m_CreatedBufferCount;
				return handle;
			}
			RHITextureViewHandle CreateTextureView(
				RHITextureHandle, const RHITextureViewDesc&) noexcept override
			{
				return {};
			}
			RHIBufferViewHandle CreateBufferView(
				RHIBufferHandle, const RHIBufferViewDesc&) noexcept override
			{
				return {};
			}
			RHISamplerHandle CreateSampler(const RHISamplerDesc&) noexcept override { return {}; }
			void DestroyTexture(RHITextureHandle) noexcept override {}
			void DestroyBuffer(RHIBufferHandle buffer) noexcept override
			{
				if (m_LiveBuffers.erase(buffer) != 0)
				{
					++m_DestroyedBufferCount;
				}
			}
			void DestroyTextureView(RHITextureViewHandle) noexcept override {}
			void DestroyBufferView(RHIBufferViewHandle) noexcept override {}
			void DestroySampler(RHISamplerHandle) noexcept override {}
			void SetTextureDebugBinding(
				RHITextureHandle, const RHIResourceDebugBindingDesc&) noexcept override
			{
			}
			void SetBufferDebugBinding(
				RHIBufferHandle, const RHIResourceDebugBindingDesc&) noexcept override
			{
			}
			std::string_view GetTextureDebugName(RHITextureHandle) const noexcept override
			{
				return {};
			}
			std::string_view GetBufferDebugName(RHIBufferHandle) const noexcept override
			{
				return {};
			}
			void* MapBuffer(RHIBufferHandle, RHIMappedBufferRange) noexcept override
			{
				return nullptr;
			}
			void UnmapBuffer(RHIBufferHandle, RHIMappedBufferRange) noexcept override {}
			uint32_t GetBufferViewAlignment(RHIBufferViewType) const noexcept override { return 1; }
			bool IsAlive(RHITextureHandle texture) const noexcept override
			{
				return texture.IsValid();
			}
			bool IsAlive(RHIBufferHandle buffer) const noexcept override
			{
				return m_LiveBuffers.contains(buffer);
			}
			bool IsAlive(RHISamplerHandle sampler) const noexcept override
			{
				return sampler.IsValid();
			}
			bool IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept override
			{
				return m_FenceCompleted && fencePoint.IsValid();
			}
			void RecordTextureUse(RHITextureHandle, const RHIFencePoint&) noexcept override {}
			void RecordBufferUse(RHIBufferHandle, const RHIFencePoint&) noexcept override {}
			RHIDescriptorHandle GetTextureViewDescriptor(
				RHITextureViewHandle) const noexcept override
			{
				return {};
			}
			RHIDescriptorHandle GetBufferViewDescriptor(
				RHIBufferViewHandle) const noexcept override
			{
				return {};
			}
			RHIDescriptorHandle GetSamplerDescriptor(
				RHISamplerHandle) const noexcept override
			{
				return {};
			}
			void RetireCompletedWork() noexcept override {}

			void CompleteFence() noexcept { m_FenceCompleted = true; }
			void FailNextBufferCreation() noexcept { m_FailNextBufferCreation = true; }
			uint32_t GetCreatedBufferCount() const noexcept { return m_CreatedBufferCount; }
			uint32_t GetDestroyedBufferCount() const noexcept { return m_DestroyedBufferCount; }
			uint32_t GetLiveBufferCount() const noexcept
			{
				return static_cast<uint32_t>(m_LiveBuffers.size());
			}

		private:
			std::unordered_set<RHIBufferHandle> m_LiveBuffers;
			uint32_t m_NextBufferIndex = 0;
			uint32_t m_CreatedBufferCount = 0;
			uint32_t m_DestroyedBufferCount = 0;
			bool m_FenceCompleted = false;
			bool m_FailNextBufferCreation = false;
		};

		class NapaVoxelPublicationTestTransferContext final : public RHITransferContext
		{
		public:
			RHICommandContextHandle GetHandle() const noexcept override { return { 1, 1 }; }
			RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Copy; }
			void TrackTextureUse(RHITextureHandle) noexcept override {}
			void TrackBufferUse(RHIBufferHandle) noexcept override {}
			void TextureBarrier(std::span<const RHITextureBarrier>) noexcept override {}
			void BufferBarrier(std::span<const RHIBufferBarrier>) noexcept override {}
			void FlushBarriers() noexcept override {}
			void CopyBuffer(RHIBufferHandle, uint64_t, RHIBufferHandle, uint64_t,
				uint64_t) noexcept override
			{
			}
			void Begin() noexcept override { m_IsRecording = true; }
			RHIFencePoint Submit(bool) noexcept override
			{
				m_IsRecording = false;
				++m_SubmissionCount;
				return { RHIFenceHandle{ 1, 1 }, m_SubmissionCount };
			}
			void Abort() noexcept override { m_IsRecording = false; }
			void ReclaimCompleted() noexcept override { ++m_ReclaimCount; }
			bool UploadBuffer(const void* data, uint64_t sizeInBytes, RHIBufferHandle destination,
				uint64_t) noexcept override
			{
				if (!m_IsRecording || !data || sizeInBytes == 0 || !destination.IsValid())
				{
					return false;
				}
				++m_UploadCount;
				return !m_FailUploads;
			}
			bool UploadTexture(
				const RHITextureUploadData&, RHITextureHandle) noexcept override
			{
				return false;
			}
			RHITextureReadbackRequest ReadbackTexture(
				RHITextureHandle, const RHITextureDesc&) noexcept override
			{
				return {};
			}

			uint32_t GetSubmissionCount() const noexcept { return m_SubmissionCount; }
			uint32_t GetUploadCount() const noexcept { return m_UploadCount; }
			void SetFailUploads(bool fail) noexcept { m_FailUploads = fail; }

		private:
			uint32_t m_SubmissionCount = 0;
			uint32_t m_UploadCount = 0;
			uint32_t m_ReclaimCount = 0;
			bool m_IsRecording = false;
			bool m_FailUploads = false;
		};

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

			return PrepareNapaVoxelInitialPublication(
				pending, stableId, ownerGeneration, publication);
		}

		[[nodiscard]] bool BuildPublicationPending(bool renderable,
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pending) noexcept
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
			constexpr std::array chunks{ ChunkCoord{} };
			CpuMeshBatch batch{};
			VisibleMeshSet visible{};
			return GeneratePrimitiveVoxelWorld(config, primitives, world, generation).Succeeded() &&
				world && BuildCpuMeshBatch(*world, 1, chunks, batch).Succeeded() &&
				ValidateCpuMeshBatch(batch, visible, pending).Succeeded() && pending;
		}

		[[nodiscard]] bool BuildReplacementPublicationInput(
			NapaVoxelPublicationTestDevice& device, double strength,
			NapaVoxelRenderState& renderState,
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pending,
			std::vector<napa::voxel::ChunkCoord>& replacementChunks) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config{
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 1.0f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = {},
					.m_MaxExclusive = { 24, 8, 8 },
					},
			};
			const std::array primitives{
				PrimitiveDesc{
					.m_StableId = { 1 },
					.m_Material = VoxelMaterial::Soil,
					.m_Shape = PrimitiveShape::Sphere,
					.m_Parameters = {
						.m_Sphere = {
							.m_Center = { 4.0, 4.0, 4.0 },
							.m_Radius = 2.0,
							},
						},
					},
				PrimitiveDesc{
					.m_StableId = { 2 },
					.m_Material = VoxelMaterial::Soil,
					.m_Shape = PrimitiveShape::Sphere,
					.m_Parameters = {
						.m_Sphere = {
							.m_Center = { 20.0, 4.0, 4.0 },
							.m_Radius = 2.0,
							},
						},
					},
			};
			std::unique_ptr<VoxelWorld> world;
			PrimitiveWorldGenerationResult generation{};
			if (GeneratePrimitiveVoxelWorld(config, primitives, world, generation).Failed() || !world)
			{
				return false;
			}

			constexpr std::array chunks{
				ChunkCoord{ 0, 0, 0 }, ChunkCoord{ 1, 0, 0 }, ChunkCoord{ 2, 0, 0 },
			};
			CpuMeshBatch initialBatch{};
			VisibleMeshSet emptyVisible{};
			std::unique_ptr<PendingCpuMeshBatch> initialPending;
			std::shared_ptr<NapaVoxelInitialPublicationOwner> initialPublication;
			if (BuildCpuMeshBatch(*world, world->GetWorldVoxelRevision(), chunks,
				initialBatch).Failed() ||
				ValidateCpuMeshBatch(initialBatch, emptyVisible, initialPending).Failed() ||
				!PrepareNapaVoxelInitialPublication(initialPending, 7001, 1,
					initialPublication) ||
				!initialPublication->PrepareGpuResources(&device) ||
				!initialPublication->MarkQueued() ||
				!initialPublication->BeginRecording(initialPublication->GetUploadIdentity()) ||
				!initialPublication->SetUploadHandle({ 701 }) ||
				!initialPublication->CompleteUpload({
					.m_Handle = initialPublication->GetUploadHandle(),
					.m_Identity = initialPublication->GetUploadIdentity(),
					.m_Status = AssetUploadStatus::Succeeded,
					.m_FencePoint = { RHIFenceHandle{ 7, 1 }, 1 },
					}))
			{
				return false;
			}

			std::unique_ptr<NapaVoxelPreparedInitialCommit> initialCommit;
			if (!renderState.PrepareInitialCommit(initialPublication, initialCommit))
			{
				return false;
			}
			renderState.CommitInitial(initialCommit);

			const SphereEditRequest edit{
				.m_Brush = {
					.m_CenterWorld = { 4.0, 4.0, 4.0 },
					.m_Radius = 3.0,
					.m_Strength = strength,
					},
			};
			VoxelMutationResult mutation{};
			CpuMeshBatch replacementBatch{};
			if (ApplySphereEdit(*world, edit, mutation).Failed() || !mutation.Changed() ||
				BuildCpuMeshBatch(*world, mutation, replacementBatch).Failed() ||
				ValidateCpuMeshBatch(replacementBatch, renderState.GetVisibleCoreMeshes(),
					pending).Failed() || !pending)
			{
				return false;
			}

			const CpuMeshReplacementView replacements = pending->GetReplacementChunks();
			replacementChunks.clear();
			replacementChunks.reserve(replacements.size());
			for (size_t index = 0; index < replacements.size(); ++index)
			{
				replacementChunks.push_back(replacements[index].m_Chunk);
			}
			return !replacementChunks.empty();
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
			std::unique_ptr<NapaVoxelPreparedInitialCommit> preparedCommit;
			const bool prepared =
				renderState.PrepareInitialCommit(publication, preparedCommit);
			if (prepared)
			{
				renderState.CommitInitial(preparedCommit);
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
				!renderState.PrepareInitialCommit(stalePublication, preparedCommit) &&
				renderState.GetVisibleWorldRevision() == 1,
				"Host revalidation rejects a same-revision initial publication before commit");
		}

		void RunInitialPublicationFailureTests(SelfTestContext& context) noexcept
		{
			std::unique_ptr<NapaVoxelPreparedInitialCommit> preparedCommit;
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
				!uploadFailureState.PrepareInitialCommit(uploadFailure, preparedCommit) &&
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

		void RunPublicationSessionCancellationIntegrationTest(SelfTestContext& context) noexcept
		{
			auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
			NapaVoxelPublicationTestTransferContext* transferContextView = transferContext.get();
			TransferManager transferManager(std::move(transferContext));
			NapaVoxelPublicationTestDevice device;
			AssetUploadScheduler scheduler({
				.m_Device = &device,
				.m_TransferManager = &transferManager,
				});

			std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
			NapaVoxelStaticPublicationSession session(&device, &scheduler);
			const bool began = BuildPublicationPending(true, pending) &&
				session.BeginPrepare(pending, 3001, 1);
			scheduler.DrainReadyWork();
			const AssetUploadStatistics submitted = scheduler.GetStatistics();
			context.Check(began && !pending &&
				session.GetPublicationStatus() ==
				NapaVoxelInitialPublicationStatus::AwaitingFence &&
				transferContextView->GetSubmissionCount() == 1 &&
				transferContextView->GetUploadCount() == 2 &&
				submitted.m_PendingCount == 1 && submitted.m_SubmittedCount == 1 &&
				device.GetCreatedBufferCount() == 2 &&
				device.GetDestroyedBufferCount() == 0,
				"Session preparation submits one durable Scheduler TransferBatch");

			session.CancelPrepare();
			context.Check(!session.IsReady() && !session.HasVisibleMeshes() &&
				!session.GetFrameView() && device.GetDestroyedBufferCount() == 0,
				"Lab switch cancellation releases Session state without retiring in-flight buffers");

			device.CompleteFence();
			GGLAB_UNUSED(scheduler.Tick());
			const AssetUploadStatistics completed = scheduler.GetStatistics();
			context.Check(completed.m_PendingCount == 0 && completed.m_SucceededCount == 1 &&
				device.GetDestroyedBufferCount() == device.GetCreatedBufferCount() &&
				!session.IsReady() && !session.HasVisibleMeshes() && !session.GetFrameView(),
				"Post-cancel Copy Fence completion retires buffers without publication or a dangling frame view");
			scheduler.Finalize();
		}

		void RunReplacementBatchContractTests(SelfTestContext& context) noexcept
		{
			NapaVoxelPublicationTestDevice device;
			NapaVoxelRenderState renderState{};
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
			std::vector<napa::voxel::ChunkCoord> expectedChunks;
			const bool fixtureBuilt = BuildReplacementPublicationInput(
				device, 10.0, renderState, pending, expectedChunks);
			const std::shared_ptr<const NapaVoxelGpuMeshSet> baseMeshes =
				renderState.GetVisibleGpuMeshes();
			std::shared_ptr<GGLabMeshPublicationBatch> publication;
			const bool prepared = fixtureBuilt && PrepareGGLabMeshPublicationBatch(
				pending, baseMeshes, {
					.m_OperationSerial = 11,
					.m_PublicationSerial = 1,
					.m_OwnerGeneration = 3,
					}, 8001, publication) && publication &&
				publication->PrepareGpuResources(&device);

			bool exactReplacementSet = prepared && !pending &&
				publication->GetReplacements().size() == expectedChunks.size();
			bool hasDelete = false;
			if (exactReplacementSet)
			{
				for (size_t index = 0; index < expectedChunks.size(); ++index)
				{
					exactReplacementSet &=
						publication->GetReplacements()[index].m_Chunk == expectedChunks[index];
					hasDelete |= publication->GetReplacements()[index].IsDelete();
				}
			}
			context.Check(exactReplacementSet && hasDelete &&
				publication->GetIdentity() == GGLabMeshPublicationIdentity{ 11, 1, 3 } &&
				publication->GetUploadIdentity().m_Kind ==
				AssetStreamingWorkKind::RuntimeMesh,
				"Replacement conversion consumes the exact Core replacement view and preserves identity");

			const std::shared_ptr<const NapaVoxelGpuMeshSet> prospective = prepared
				? publication->GetProspectiveGpuMeshes()
				: nullptr;
			bool reusedUntouchedChunk = false;
			if (baseMeshes && prospective)
			{
				for (const auto& base : baseMeshes->GetChunks())
				{
					for (const auto& candidate : prospective->GetChunks())
					{
						if (base && candidate && base->m_Chunk == napa::voxel::ChunkCoord{ 2, 0, 0 } &&
							candidate->m_Chunk == base->m_Chunk && candidate == base)
						{
							reusedUntouchedChunk = true;
						}
					}
				}
			}
			context.Check(prospective && prospective->GetVisibleWorldRevision() == 2 &&
				reusedUntouchedChunk && renderState.GetVisibleWorldRevision() == 1 &&
				renderState.GetVisibleGpuMeshes() == baseMeshes,
				"Prospective replacement state reuses untouched buffers without changing Visible state");

			NapaVoxelRenderState exhaustedState{};
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch> exhaustedPending;
			std::vector<napa::voxel::ChunkCoord> exhaustedChunks;
			std::shared_ptr<GGLabMeshPublicationBatch> exhausted;
			const bool exhaustedPrepared = BuildReplacementPublicationInput(
				device, 0.1, exhaustedState, exhaustedPending, exhaustedChunks) &&
				PrepareGGLabMeshPublicationBatch(exhaustedPending,
					exhaustedState.GetVisibleGpuMeshes(), {
						.m_OperationSerial = 12,
						.m_PublicationSerial = 2,
						.m_OwnerGeneration = std::numeric_limits<uint64_t>::max(),
						}, 8002, exhausted);
			const AssetStreamingIdentity exhaustedIdentity = exhaustedPrepared
				? exhausted->GetUploadIdentity()
				: AssetStreamingIdentity{};
			if (exhausted)
			{
				exhausted->Cancel();
			}
			context.Check(exhaustedPrepared && exhausted->IsOwnerGenerationExhausted() &&
				exhausted->GetIdentity().m_OwnerGeneration ==
				std::numeric_limits<uint64_t>::max() &&
				!exhausted->BeginRecording(exhaustedIdentity),
				"Replacement owner-generation cancellation cannot wrap or revive stale work");
		}

		void RunReplacementUploadCompletionTests(SelfTestContext& context) noexcept
		{
			auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
			NapaVoxelPublicationTestTransferContext* transferView = transferContext.get();
			TransferManager transferManager(std::move(transferContext));
			NapaVoxelPublicationTestDevice device;
			AssetUploadScheduler scheduler({
				.m_Device = &device,
				.m_TransferManager = &transferManager,
				});
			NapaVoxelCommandQueue commandQueue;
			NapaVoxelRenderState renderState{};
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
			std::vector<napa::voxel::ChunkCoord> replacementChunks;
			const bool fixtureBuilt = BuildReplacementPublicationInput(
				device, 0.1, renderState, pending, replacementChunks);
			const auto visibleBefore = renderState.GetVisibleGpuMeshes();
			NapaVoxelMeshReplacementUploadSession session(
				&device, &scheduler, &commandQueue);
			const bool began = fixtureBuilt && session.BeginPrepare(
				pending, visibleBefore, 21, 1);
			const auto publication = session.GetPublication();
			scheduler.DrainReadyWork();
			const AssetUploadStatistics submitted = scheduler.GetStatistics();
			context.Check(began && !pending && publication &&
				publication->GetStatus() == GGLabMeshPublicationStatus::AwaitingFence &&
				submitted.m_UploadRecordingQueue.m_EnqueuedCount == 1 &&
				submitted.m_SubmittedCount == 1 && submitted.m_BatchSubmissionCount == 1 &&
				transferView->GetSubmissionCount() == 1 &&
				transferView->GetUploadCount() ==
				publication->GetUploadEstimate().m_OperationCount,
				"One replacement publication records one Scheduler work item, handle, and fence batch");

			device.CompleteFence();
			GGLAB_UNUSED(scheduler.Tick());
			context.Check(publication->IsReadyForCommit() && !session.IsReady() &&
				renderState.GetVisibleWorldRevision() == 1 &&
				renderState.GetVisibleGpuMeshes() == visibleBefore,
				"Copy completion marks only the durable batch ready before the next owner Update");
			session.TickPrepare();
			context.Check(session.IsReady() && !session.HasFailed() &&
				!commandQueue.IsTerminal() && renderState.GetVisibleWorldRevision() == 1 &&
				renderState.GetVisibleGpuMeshes() == visibleBefore,
				"Owner Update observes upload readiness without publishing replacement state early");
			session.CancelPrepare();
			scheduler.Finalize();
		}

		void RunEmptyReplacementUploadTest(SelfTestContext& context) noexcept
		{
			auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
			NapaVoxelPublicationTestTransferContext* transferView = transferContext.get();
			TransferManager transferManager(std::move(transferContext));
			NapaVoxelPublicationTestDevice device;
			AssetUploadScheduler scheduler({
				.m_Device = &device,
				.m_TransferManager = &transferManager,
				});
			NapaVoxelCommandQueue commandQueue;
			NapaVoxelRenderState renderState{};
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
			std::vector<napa::voxel::ChunkCoord> replacementChunks;
			const bool fixtureBuilt = BuildReplacementPublicationInput(
				device, 10.0, renderState, pending, replacementChunks);
			NapaVoxelMeshReplacementUploadSession session(
				&device, &scheduler, &commandQueue);
			const bool began = fixtureBuilt && session.BeginPrepare(
				pending, renderState.GetVisibleGpuMeshes(), 22, 1);
			const auto publication = session.GetPublication();
			bool allDeletes = began && publication && !publication->GetReplacements().empty();
			if (allDeletes)
			{
				for (const NapaVoxelGpuChunkReplacement& replacement :
					publication->GetReplacements())
				{
					allDeletes &= replacement.IsDelete();
				}
			}
			scheduler.DrainReadyWork();
			device.CompleteFence();
			GGLAB_UNUSED(scheduler.Tick());
			session.TickPrepare();
			context.Check(allDeletes && publication->GetUploadEstimate().m_OperationCount == 0 &&
				transferView->GetUploadCount() == 0 && transferView->GetSubmissionCount() == 1 &&
				scheduler.GetStatistics().m_SubmittedCount == 1 && session.IsReady() &&
				renderState.GetVisibleWorldRevision() == 1,
				"An Empty/Delete replacement still receives one durable Scheduler handle and fence without buffer uploads");
			session.CancelPrepare();
			scheduler.Finalize();
		}

		void RunReplacementUploadFailureTests(SelfTestContext& context) noexcept
		{
			{
				auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
				NapaVoxelPublicationTestTransferContext* transferView = transferContext.get();
				TransferManager transferManager(std::move(transferContext));
				NapaVoxelPublicationTestDevice device;
				AssetUploadScheduler scheduler({
					.m_Device = &device,
					.m_TransferManager = &transferManager,
					});
				NapaVoxelCommandQueue commandQueue;
				NapaVoxelRenderState renderState{};
				std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
				std::vector<napa::voxel::ChunkCoord> replacementChunks;
				const bool fixtureBuilt = BuildReplacementPublicationInput(
					device, 0.1, renderState, pending, replacementChunks);
				NapaVoxelMeshReplacementUploadSession session(&device, &scheduler, &commandQueue, {
					.m_LastPublicationSerial = std::numeric_limits<uint64_t>::max(),
					});
				const bool began = fixtureBuilt && session.BeginPrepare(
					pending, renderState.GetVisibleGpuMeshes(), 31, 1);
				const AssetUploadStatistics statistics = scheduler.GetStatistics();
				context.Check(!began && pending && !session.GetPublication() && session.HasFailed() &&
					session.GetLastPublicationSerial() == std::numeric_limits<uint64_t>::max() &&
					commandQueue.GetTerminalError() ==
					NapaVoxelCommandQueueError::PublicationSerialExhausted &&
					statistics.m_UploadRecordingQueue.m_EnqueuedCount == 0 &&
					statistics.m_SubmittedCount == 0 && transferView->GetUploadCount() == 0 &&
					transferView->GetSubmissionCount() == 0,
					"Publication serial exhaustion freezes FIFO without consuming pending data or scheduling upload work");
				scheduler.Finalize();
			}

			{
				auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
				TransferManager transferManager(std::move(transferContext));
				NapaVoxelPublicationTestDevice device;
				AssetUploadScheduler scheduler({
					.m_Device = &device,
					.m_TransferManager = &transferManager,
					});
				NapaVoxelCommandQueue commandQueue;
				NapaVoxelRenderState renderState{};
				std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
				std::vector<napa::voxel::ChunkCoord> replacementChunks;
				const bool fixtureBuilt = BuildReplacementPublicationInput(
					device, 0.1, renderState, pending, replacementChunks);
				const auto visibleBefore = renderState.GetVisibleGpuMeshes();
				device.FailNextBufferCreation();
				NapaVoxelMeshReplacementUploadSession session(&device, &scheduler, &commandQueue);
				const bool began = fixtureBuilt && session.BeginPrepare(
					pending, visibleBefore, 32, 1);
				context.Check(!began && !pending && session.HasFailed() &&
					commandQueue.GetTerminalError() ==
					NapaVoxelCommandQueueError::HostPreparationFailed &&
					renderState.GetVisibleWorldRevision() == 1 &&
					renderState.GetVisibleGpuMeshes() == visibleBefore &&
					scheduler.GetStatistics().m_UploadRecordingQueue.m_EnqueuedCount == 0,
					"GPU resource creation failure freezes FIFO and preserves the complete old Visible state");
				scheduler.Finalize();
			}

			{
				auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
				NapaVoxelPublicationTestTransferContext* transferView = transferContext.get();
				transferView->SetFailUploads(true);
				TransferManager transferManager(std::move(transferContext));
				NapaVoxelPublicationTestDevice device;
				AssetUploadScheduler scheduler({
					.m_Device = &device,
					.m_TransferManager = &transferManager,
					});
				NapaVoxelCommandQueue commandQueue;
				NapaVoxelRenderState renderState{};
				std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
				std::vector<napa::voxel::ChunkCoord> replacementChunks;
				const bool fixtureBuilt = BuildReplacementPublicationInput(
					device, 0.1, renderState, pending, replacementChunks);
				const auto visibleBefore = renderState.GetVisibleGpuMeshes();
				NapaVoxelMeshReplacementUploadSession session(&device, &scheduler, &commandQueue);
				const bool began = fixtureBuilt && session.BeginPrepare(
					pending, visibleBefore, 33, 1);
				scheduler.DrainReadyWork();
				device.CompleteFence();
				GGLAB_UNUSED(scheduler.Tick());
				session.TickPrepare();
				context.Check(began && session.HasFailed() && !session.IsReady() &&
					commandQueue.GetTerminalError() ==
					NapaVoxelCommandQueueError::HostPreparationFailed &&
					renderState.GetVisibleWorldRevision() == 1 &&
					renderState.GetVisibleGpuMeshes() == visibleBefore,
					"Upload failure is observed on owner Update and freezes FIFO without partial publication");
				session.CancelPrepare();
				scheduler.Finalize();
			}
		}

		void RunReplacementUploadCancellationTests(SelfTestContext& context) noexcept
		{
			{
				auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
				NapaVoxelPublicationTestTransferContext* transferView = transferContext.get();
				TransferManager transferManager(std::move(transferContext));
				NapaVoxelPublicationTestDevice device;
				AssetUploadScheduler scheduler({
					.m_Device = &device,
					.m_TransferManager = &transferManager,
					});
				NapaVoxelCommandQueue commandQueue;
				NapaVoxelRenderState renderState{};
				std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
				std::vector<napa::voxel::ChunkCoord> replacementChunks;
				const bool fixtureBuilt = BuildReplacementPublicationInput(
					device, 0.1, renderState, pending, replacementChunks);
				NapaVoxelMeshReplacementUploadSession session(&device, &scheduler, &commandQueue);
				const bool began = fixtureBuilt && session.BeginPrepare(
					pending, renderState.GetVisibleGpuMeshes(), 41, 1);
				std::weak_ptr<const NapaVoxelGpuMeshSet> prospective = began
					? session.GetPublication()->GetProspectiveGpuMeshes()
					: std::shared_ptr<const NapaVoxelGpuMeshSet>{};
				const uint32_t destroyedBefore = device.GetDestroyedBufferCount();
				session.CancelPrepare();
				context.Check(began && prospective.expired() &&
					device.GetDestroyedBufferCount() > destroyedBefore &&
					transferView->GetUploadCount() == 0 &&
					transferView->GetSubmissionCount() == 0 &&
					scheduler.GetStatistics().m_UploadRecordingQueue.m_CancelledCount == 1 &&
					!commandQueue.IsTerminal(),
					"Recording-before cancellation removes ready work and retires unsubmitted replacement buffers");
				scheduler.Finalize();
			}

			{
				auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
				TransferManager transferManager(std::move(transferContext));
				NapaVoxelPublicationTestDevice device;
				AssetUploadScheduler scheduler({
					.m_Device = &device,
					.m_TransferManager = &transferManager,
					});
				NapaVoxelCommandQueue commandQueue;
				NapaVoxelRenderState renderState{};
				std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
				std::vector<napa::voxel::ChunkCoord> replacementChunks;
				const bool fixtureBuilt = BuildReplacementPublicationInput(
					device, 0.1, renderState, pending, replacementChunks);
				const auto visibleBefore = renderState.GetVisibleGpuMeshes();
				NapaVoxelMeshReplacementUploadSession session(&device, &scheduler, &commandQueue);
				const bool began = fixtureBuilt && session.BeginPrepare(
					pending, visibleBefore, 42, 7);
				std::weak_ptr<const NapaVoxelGpuMeshSet> prospective = began
					? session.GetPublication()->GetProspectiveGpuMeshes()
					: std::shared_ptr<const NapaVoxelGpuMeshSet>{};
				scheduler.DrainReadyWork();
				const uint32_t destroyedBeforeCancel = device.GetDestroyedBufferCount();
				session.CancelPrepare();
				const bool retainedThroughFence = !prospective.expired() &&
					device.GetDestroyedBufferCount() == destroyedBeforeCancel;
				device.CompleteFence();
				GGLAB_UNUSED(scheduler.Tick());
				context.Check(began && retainedThroughFence && prospective.expired() &&
					device.GetDestroyedBufferCount() > destroyedBeforeCancel &&
					renderState.GetVisibleWorldRevision() == 1 &&
					renderState.GetVisibleGpuMeshes() == visibleBefore &&
					!session.IsReady() && !commandQueue.IsTerminal(),
					"Recording-after cancellation retains buffers through Copy Fence and never publishes a frame view");
				scheduler.Finalize();
			}
		}
	}

	void RunNapaVoxelPublicationSelfTests(SelfTestContext& context) noexcept
	{
		RunInitialPublicationStateTests(context);
		RunInitialPublicationFailureTests(context);
		RunInitialPublicationCancellationTests(context);
		RunInitialPublicationIdentityTests(context);
		RunPublicationSessionCancellationIntegrationTest(context);
		RunReplacementBatchContractTests(context);
		RunReplacementUploadCompletionTests(context);
		RunEmptyReplacementUploadTest(context);
		RunReplacementUploadFailureTests(context);
		RunReplacementUploadCancellationTests(context);
	}
}
