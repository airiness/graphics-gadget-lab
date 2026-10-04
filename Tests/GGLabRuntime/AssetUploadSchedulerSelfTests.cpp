#include "AssetUploadSchedulerSelfTests.h"
#include "AssetTestServices.h"

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
				m_TransferManager(std::make_unique<asset_test::TestTransferContext>()),
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

			asset_test::TestRHIDevice m_Device;
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
			auto transferContext = std::make_unique<asset_test::TestTransferContext>();
			TransferManager transferManager(std::move(transferContext));
			asset_test::TestRHIDevice device;
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
