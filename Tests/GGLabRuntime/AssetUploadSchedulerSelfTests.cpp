#include "AssetUploadSchedulerSelfTests.h"

#include "GGLabRuntime/Graphics/Asset/AssetResourcePublication.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadScheduling.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDescriptor.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHISampler.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHITransferContext.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/TransferManager.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace gglab
{
	namespace
	{
		class NapaVoxelPublicationTestDevice final : public RHIDevice
		{
		public:
			RHIBackendType GetBackendType() const noexcept override
			{
				return RHIBackendType::DX12;
			}
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
				return { .m_Supported = true };
			}
			RHITextureSupportResult QueryTextureViewSupport(
				const RHITextureDesc&, const RHITextureViewDesc&) const noexcept override
			{
				return { .m_Supported = true };
			}
			RHITextureHandle CreateTexture(const RHIOwnedTextureCreateInfo&,
				const RHIResourceDebugIdentityDesc&) noexcept override
			{
				return { m_NextTextureIndex++, 1 };
			}
			RHIBufferHandle CreateBuffer(
				const RHIBufferDesc& desc, const RHIResourceDebugIdentityDesc&) noexcept override
			{
				if (m_FailNextBufferCreation)
				{
					m_FailNextBufferCreation = false;
					return {};
				}
				const RHIBufferHandle handle{ m_NextBufferIndex++, 1 };
				m_LiveBuffers.emplace(handle, desc.m_SizeInBytes);
				m_LiveBufferBytes += desc.m_SizeInBytes;
				++m_CreatedBufferCount;
				return handle;
			}
			RHITextureViewHandle CreateTextureView(
				RHITextureHandle, const RHITextureViewDesc&) noexcept override
			{
				return { m_NextTextureViewIndex++, 1 };
			}
			RHIBufferViewHandle CreateBufferView(
				RHIBufferHandle, const RHIBufferViewDesc&) noexcept override
			{
				return {};
			}
			RHISamplerHandle CreateSampler(const RHISamplerDesc&) noexcept override
			{
				return { m_NextSamplerIndex++, 1 };
			}
			void DestroyTexture(RHITextureHandle) noexcept override {}
			void DestroyBuffer(RHIBufferHandle buffer) noexcept override
			{
				const auto found = m_LiveBuffers.find(buffer);
				if (found != m_LiveBuffers.end())
				{
					m_LiveBufferBytes -= found->second;
					m_LiveBuffers.erase(found);
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
				if (!fencePoint.IsValid())
				{
					return false;
				}
				if (m_AllFencesCompleted)
				{
					return true;
				}
				for (const RHIFencePoint& completed : m_CompletedFencePoints)
				{
					if (completed.m_Fence == fencePoint.m_Fence &&
						completed.m_Value >= fencePoint.m_Value)
					{
						return true;
					}
				}
				return false;
			}
			void RecordTextureUse(RHITextureHandle, const RHIFencePoint&) noexcept override {}
			void RecordBufferUse(RHIBufferHandle, const RHIFencePoint&) noexcept override {}
			RHIDescriptorHandle GetTextureViewDescriptor(
				RHITextureViewHandle view) const noexcept override
			{
				return {
					.m_HeapType = RHIDescriptorHeapType::CbvSrvUav,
					.m_Index = view.Index(),
				};
			}
			RHIDescriptorHandle GetBufferViewDescriptor(
				RHIBufferViewHandle) const noexcept override
			{
				return {};
			}
			RHIDescriptorHandle GetSamplerDescriptor(
				RHISamplerHandle sampler) const noexcept override
			{
				return {
					.m_HeapType = RHIDescriptorHeapType::Sampler,
					.m_Index = sampler.Index(),
				};
			}
			void RetireCompletedWork() noexcept override {}

			void CompleteFence() noexcept { m_AllFencesCompleted = true; }
			void CompleteFence(RHIFencePoint fencePoint)
			{
				for (RHIFencePoint& completed : m_CompletedFencePoints)
				{
					if (completed.m_Fence == fencePoint.m_Fence)
					{
						completed.m_Value = std::max(completed.m_Value, fencePoint.m_Value);
						return;
					}
				}
				m_CompletedFencePoints.push_back(fencePoint);
			}
			void FailNextBufferCreation() noexcept { m_FailNextBufferCreation = true; }
			uint32_t GetCreatedBufferCount() const noexcept { return m_CreatedBufferCount; }
			uint32_t GetDestroyedBufferCount() const noexcept { return m_DestroyedBufferCount; }
			uint32_t GetLiveBufferCount() const noexcept
			{
				return static_cast<uint32_t>(m_LiveBuffers.size());
			}
			uint64_t GetLiveBufferBytes() const noexcept { return m_LiveBufferBytes; }

		private:
			std::unordered_map<RHIBufferHandle, uint64_t> m_LiveBuffers;
			std::vector<RHIFencePoint> m_CompletedFencePoints;
			uint64_t m_LiveBufferBytes = 0;
			uint32_t m_NextTextureIndex = 0;
			uint32_t m_NextTextureViewIndex = 0;
			uint32_t m_NextSamplerIndex = 0;
			uint32_t m_NextBufferIndex = 0;
			uint32_t m_CreatedBufferCount = 0;
			uint32_t m_DestroyedBufferCount = 0;
			bool m_AllFencesCompleted = false;
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
				const RHITextureUploadData& data, RHITextureHandle destination) noexcept override
			{
				return m_IsRecording && data.IsValid() && destination.IsValid();
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

		struct SyntheticPublicationState
		{
			uint32_t m_TargetSteps = 1;
			uint32_t m_Steps = 0;
			uint64_t m_ProgressToken = 0;
			uint32_t m_AbortCount = 0;
			AssetResourcePublicationAbortReason m_AbortReason =
				AssetResourcePublicationAbortReason::Shutdown;
			bool m_Completed = false;
		};

		// Finite-progress job that yields once per step until its target is reached.
		class SyntheticPublicationJob final : public IResourcePublicationJob
		{
		public:
			explicit SyntheticPublicationJob(
				std::shared_ptr<SyntheticPublicationState> state) noexcept :
				m_State(std::move(state))
			{
			}

			[[nodiscard]] AssetResourcePublicationStepResult Step(
				AssetResourcePublicationContext&) noexcept override
			{
				++m_State->m_Steps;
				++m_State->m_ProgressToken;
				m_State->m_Completed = m_State->m_Steps >= m_State->m_TargetSteps;
				return {
					.m_Status = m_State->m_Completed ? AssetResourcePublicationStepStatus::Completed
						: AssetResourcePublicationStepStatus::Continue,
					.m_Usage = {.m_Stage = AssetResourcePublicationStage::Textures},
				};
			}

			void Abort(AssetResourcePublicationContext&,
				AssetResourcePublicationAbortReason reason) noexcept override
			{
				++m_State->m_AbortCount;
				m_State->m_AbortReason = reason;
			}

			[[nodiscard]] uint64_t GetProgressToken() const noexcept override
			{
				return m_State->m_ProgressToken;
			}

			[[nodiscard]] AssetResourcePublicationStage GetCurrentStage() const noexcept override
			{
				return AssetResourcePublicationStage::Textures;
			}

		private:
			std::shared_ptr<SyntheticPublicationState> m_State;
		};

		// Scheduler over fake RHI services. One publication step per Tick reproduces
		// frame-by-frame interleaving without depending on wall-clock budgets.
		struct PublicationSchedulerFixture
		{
			PublicationSchedulerFixture() noexcept :
				m_TransferManager(std::make_unique<NapaVoxelPublicationTestTransferContext>()),
				m_Scheduler(CreateAssetUploadScheduler({
					.m_Device = &m_Device,
					.m_TransferManager = &m_TransferManager,
					.m_FrameBudget = {
						.m_MaxResourcePublicationSteps = 1,
						.m_MaxResourcePublicationCreations = 1,
						.m_MaxResourcePublicationMilliseconds = 1000.0,
					},
					}))
			{
			}

			~PublicationSchedulerFixture()
			{
				m_Scheduler.m_Scheduling->Finalize();
			}

			[[nodiscard]] AssetUploadScheduling& Scheduling() const noexcept
			{
				return *m_Scheduler.m_Scheduling;
			}

			void Enqueue(const char* name, const AssetStreamingIdentity& identity,
				std::shared_ptr<SyntheticPublicationState> state) const noexcept
			{
				Scheduling().EnqueueResourcePublication(
					{ .m_Name = name, .m_Identity = identity, .m_Priority = TaskPriority::Normal },
					std::make_unique<SyntheticPublicationJob>(std::move(state)));
			}

			NapaVoxelPublicationTestDevice m_Device;
			TransferManager m_TransferManager;
			AssetUploadSchedulerInstance m_Scheduler;
		};

		[[nodiscard]] bool HasPendingPublication(
			const AssetUploadStatistics& statistics, const AssetStreamingIdentity& identity) noexcept
		{
			return std::ranges::any_of(statistics.m_ResourcePublicationQueue.m_PendingWork,
				[&identity](const AssetStreamingWorkActivity& work) noexcept
				{ return work.m_Identity == identity; });
		}

		void RunResourcePublicationGenerationTest(SelfTestContext& context) noexcept
		{
			PublicationSchedulerFixture fixture;
			constexpr uint64_t StableId = std::numeric_limits<uint64_t>::max() - 1024;
			const AssetStreamingIdentity oldIdentity{
				.m_Kind = AssetStreamingWorkKind::Model, .m_StableId = StableId, .m_Generation = 1 };
			const AssetStreamingIdentity newIdentity{
				.m_Kind = AssetStreamingWorkKind::Model, .m_StableId = StableId, .m_Generation = 2 };
			auto oldGeneration = std::make_shared<SyntheticPublicationState>();
			oldGeneration->m_TargetSteps = 8;
			auto newGeneration = std::make_shared<SyntheticPublicationState>();
			newGeneration->m_TargetSteps = 3;

			fixture.Enqueue("Stale generation 1", oldIdentity, oldGeneration);
			GGLAB_UNUSED(fixture.Scheduling().Tick());
			context.Check(oldGeneration->m_Steps == 1 && !oldGeneration->m_Completed,
				"A one-step publication budget yields the older generation after one step");

			fixture.Enqueue("Current generation 2", newIdentity, newGeneration);
			const uint32_t cancelled = fixture.Scheduling().CancelReadyWork(oldIdentity);
			const uint32_t oldStepsAtCancellation = oldGeneration->m_Steps;
			for (uint32_t tick = 0; tick < 16 && !newGeneration->m_Completed; ++tick)
			{
				GGLAB_UNUSED(fixture.Scheduling().Tick());
			}

			const AssetUploadStatistics statistics = fixture.Scheduling().GetStatistics();
			const auto& publication = statistics.m_ResourcePublicationQueue;
			context.Check(cancelled == 1 && oldGeneration->m_AbortCount == 1 &&
				oldGeneration->m_AbortReason == AssetResourcePublicationAbortReason::Cancelled &&
				oldGeneration->m_Steps == oldStepsAtCancellation,
				"Cancelling a stale generation aborts it once as Cancelled and never steps it again");
			context.Check(newGeneration->m_Completed && newGeneration->m_AbortCount == 0 &&
				newGeneration->m_Steps == newGeneration->m_TargetSteps &&
				!HasPendingPublication(statistics, oldIdentity) &&
				!HasPendingPublication(statistics, newIdentity),
				"The current generation of the same asset completes independently of the cancelled one");
			context.Check(publication.m_NoProgressContinueCount == 0 &&
				publication.m_EnqueuedCount == publication.m_CompletedCount + publication.m_FailedCount +
					publication.m_CancelledCount + publication.m_PendingCount &&
				publication.m_QueueSampleCount == publication.m_EnqueuedCount,
				"Yielded publication jobs keep queue conservation and their first-queued timestamp");
		}

		void RunResourcePublicationDrainTest(SelfTestContext& context) noexcept
		{
			PublicationSchedulerFixture fixture;
			const AssetStreamingIdentity identity{
				.m_Kind = AssetStreamingWorkKind::Model,
				.m_StableId = std::numeric_limits<uint64_t>::max() - 2048,
				.m_Generation = 1,
			};
			auto drainJob = std::make_shared<SyntheticPublicationState>();
			drainJob->m_TargetSteps = 64;
			fixture.Enqueue("Shutdown drain", identity, drainJob);
			fixture.Scheduling().DrainReadyWork();

			const AssetUploadStatistics statistics = fixture.Scheduling().GetStatistics();
			context.Check(drainJob->m_Completed && drainJob->m_Steps == drainJob->m_TargetSteps &&
				drainJob->m_AbortCount == 0 && !HasPendingPublication(statistics, identity) &&
				statistics.m_ResourcePublicationQueue.m_NoProgressContinueCount == 0,
				"DrainReadyWork completes every yielded step of a finite-progress job without aborting it");
		}

		void RunAssetUploadSchedulerWorkerHandoffTest(SelfTestContext& context) noexcept
		{
			auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
			TransferManager transferManager(std::move(transferContext));
			NapaVoxelPublicationTestDevice device;
			AssetUploadSchedulerInstance scheduler = CreateAssetUploadScheduler({
				.m_Device = &device,
				.m_TransferManager = &transferManager,
				});
			const std::thread::id ownerThreadId = std::this_thread::get_id();
			std::thread::id cpuCallbackThreadId;
			std::thread::id uploadCallbackThreadId;
			bool workerSawOwner = true;
			bool cpuCallbackRan = false;
			bool uploadCallbackRan = false;

			std::thread worker([&]()
				{
					workerSawOwner = scheduler.m_Scheduling->IsOwnerThread();
					scheduler.m_Scheduling->EnqueueCpuPayload({
						.m_Name = "Worker immutable payload handoff",
						.m_Identity = {
							.m_Kind = AssetStreamingWorkKind::Texture,
							.m_StableId = 9001,
							.m_Generation = 1,
							},
						.m_Estimate = {.m_SourceBytes = 16 },
						},
						[&]()
						{
							cpuCallbackRan = true;
							cpuCallbackThreadId = std::this_thread::get_id();
							scheduler.m_Scheduling->EnqueueUploadRecording({
								.m_Name = "Owner upload promotion",
								.m_Identity = {
									.m_Kind = AssetStreamingWorkKind::Texture,
									.m_StableId = 9001,
									.m_Generation = 1,
									},
								},
								[&]()
								{
									uploadCallbackRan = true;
									uploadCallbackThreadId = std::this_thread::get_id();
								});
						});
				});
			worker.join();

			context.Check(!workerSawOwner && !cpuCallbackRan && !uploadCallbackRan,
				"Worker enqueue hands off immutable payload without executing owner work inline");
			scheduler.m_Scheduling->DrainReadyWork();
			const AssetUploadStatistics statistics = scheduler.m_Scheduling->GetStatistics();
			context.Check(cpuCallbackRan && uploadCallbackRan &&
				cpuCallbackThreadId == ownerThreadId && uploadCallbackThreadId == ownerThreadId &&
				statistics.m_CpuPayloadQueue.m_EnqueuedCount == 1 &&
				statistics.m_CpuPayloadQueue.m_ProcessedCount == 1 &&
				statistics.m_UploadRecordingQueue.m_EnqueuedCount == 1 &&
				statistics.m_UploadRecordingQueue.m_ProcessedCount == 1,
				"Scheduler drains worker handoff and every publication/upload callback on its owner thread");
			scheduler.m_Scheduling->Finalize();
		}

	}

	void RunAssetUploadSchedulerSelfTests(SelfTestContext& context) noexcept
	{
		RunAssetUploadSchedulerWorkerHandoffTest(context);
		RunResourcePublicationGenerationTest(context);
		RunResourcePublicationDrainTest(context);
	}
}
