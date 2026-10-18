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
			AssetResourcePublicationAbortReason m_AbortReason = AssetResourcePublicationAbortReason::Shutdown;
			bool m_Completed = false;
		};

		// Finite-progress job that yields once per step until its target is reached.
		class SyntheticPublicationJob final : public IResourcePublicationJob
		{
		public:
			explicit SyntheticPublicationJob(std::shared_ptr<SyntheticPublicationState> state) noexcept :
				m_State(std::move(state))
			{
			}

			[[nodiscard]] AssetResourcePublicationStepResult Step(AssetResourcePublicationContext&) noexcept override
			{
				++m_State->m_Steps;
				++m_State->m_ProgressToken;
				m_State->m_Completed = m_State->m_Steps >= m_State->m_TargetSteps;
				return {
					.m_Status = m_State->m_Completed ?
						AssetResourcePublicationStepStatus::Completed :
						AssetResourcePublicationStepStatus::Continue,
					.m_Usage = { .m_Stage = AssetResourcePublicationStage::Textures },
				};
			}

			void Abort(AssetResourcePublicationContext&, AssetResourcePublicationAbortReason reason) noexcept override
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

			void Enqueue(const char* name, const AssetStreamingIdentity& identity, std::shared_ptr<SyntheticPublicationState> state) const noexcept
			{
				Scheduling().EnqueueResourcePublication(
					{ .m_Name = name, .m_Identity = identity, .m_Priority = TaskPriority::Normal },
					std::make_unique<SyntheticPublicationJob>(std::move(state)));
			}

			asset_test::TestRHIDevice m_Device;
			TransferManager m_TransferManager;
			AssetUploadSchedulerInstance m_Scheduler;
		};

		[[nodiscard]] bool HasPendingPublication(const AssetUploadStatistics& statistics, const AssetStreamingIdentity& identity) noexcept
		{
			return std::ranges::any_of(statistics.m_ResourcePublicationQueue.m_PendingWork,
				[&identity](const AssetStreamingWorkActivity& work) noexcept
				{
					return work.m_Identity == identity;
				});
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
						.m_Estimate = { .m_SourceBytes = 16 },
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

		void RunCompletionFailureDuringRecordingTest(SelfTestContext& context) noexcept
		{
			asset_test::TestRHIDevice device;
			auto nativeContext = std::make_unique<asset_test::TestTransferContext>();
			auto* nativeTransfer = nativeContext.get();
			TransferManager transfer(std::move(nativeContext));
			auto instance = CreateAssetUploadScheduler({ .m_Device = &device, .m_TransferManager = &transfer });
			AssetUploadScheduling& scheduler = *instance.m_Scheduling;
			uint32_t completions = 0;
			bool succeeded = false;
			uint32_t laterRecordings = 0;
			scheduler.EnqueueUploadRecording({ .m_Name = "Fail during recording" },
				[&]()
				{
					GGLAB_UNUSED(scheduler.RecordUpload({ .m_Name = "Unsubmitted recording" },
						[&](TransferBatch&)
						{
							device.SetFenceQueryResult(RHIFenceQueryResult::Failed);
							GGLAB_UNUSED(device.QueryFencePoint({ RHIFenceHandle{ 1, 1 }, 3 }));
							return true;
						},
						[&](const AssetUploadCompletionInfo& completion)
						{
							++completions;
							succeeded = completion.m_Status == AssetUploadStatus::Succeeded;
						}));
				});
			scheduler.EnqueueUploadRecording({ .m_Name = "Later recording" }, [&]() { ++laterRecordings; });
			GGLAB_UNUSED(scheduler.Tick());
			scheduler.DrainReadyWork();
			context.Check(!nativeTransfer->GetLastSubmittedFence().IsValid() &&
				completions == 0 && laterRecordings == 0,
				"A completion failure during recording retains the open transfer batch and prevents its flush and later recordings");
			scheduler.Finalize();
			context.Check(!nativeTransfer->GetLastSubmittedFence().IsValid() &&
				completions == 1 && !succeeded && laterRecordings == 0,
				"Quiescent finalization aborts an unsubmitted batch and reports failure once without a native submit");
		}

		void RunStoppedGpuWorkTest(SelfTestContext& context) noexcept
		{
			for (uint32_t scenario = 0; scenario < 3; ++scenario)
			{
				asset_test::TestRHIDevice device;
				device.SetFencesComplete(false);
				auto transferContext = std::make_unique<asset_test::TestTransferContext>();
				auto* nativeTransfer = transferContext.get();
				TransferManager transfer(std::move(transferContext));
				auto instance = CreateAssetUploadScheduler({ .m_Device = &device, .m_TransferManager = &transfer });
				AssetUploadScheduling& scheduler = *instance.m_Scheduling;
				uint32_t completionCount = 0;
				AssetUploadStatus completionStatus = AssetUploadStatus::Pending;
				scheduler.EnqueueUploadRecording({ .m_Name = "In-flight upload" },
					[&]()
					{
						GGLAB_UNUSED(scheduler.RecordUpload({ .m_Name = "In-flight upload",
							.m_Identity = { .m_Kind = AssetStreamingWorkKind::Mesh,
								.m_StableId = 123, .m_Generation = 1 },
							.m_Estimate = { .m_StagingBytes = 64 } },
							[](TransferBatch&) { return true; },
							[&](const AssetUploadCompletionInfo& info)
							{
								++completionCount;
								completionStatus = info.m_Status;
							}));
					});
				GGLAB_UNUSED(scheduler.Tick());
				const RHIFencePoint submitted = nativeTransfer->GetLastSubmittedFence();
				context.Check(submitted.IsValid() && completionCount == 0,
					"A stopped-work regression begins with a genuinely submitted pending upload");

				uint32_t cpuExecutions = 0;
				uint32_t recordingExecutions = 0;
				auto readyPayload = std::make_shared<uint32_t>(7);
				std::weak_ptr<uint32_t> retainedPayload = readyPayload;
				std::thread worker(
					[&]()
					{
						scheduler.EnqueueCpuPayload({ .m_Name = "Worker handoff" }, [&]() { ++cpuExecutions; });
					});
				worker.join();
				scheduler.EnqueueUploadRecording({ .m_Name = "Unrecorded upload",
					.m_Estimate = { .m_SourceBytes = 16 } },
					[&, payload = std::move(readyPayload)]() { recordingExecutions += *payload; });
				auto publication = std::make_shared<SyntheticPublicationState>();
				scheduler.EnqueueResourcePublication({ .m_Name = "Unstarted publication",
					.m_Estimate = { .m_SourceBytes = 32 } },
					std::make_unique<SyntheticPublicationJob>(publication));

				if (scenario == 0)
				{
					// A CompletionUnknown context explicitly stops work even without a query error.
					scheduler.StopGpuWork();
				}
				else
				{
					device.SetFenceQueryResult(scenario == 1 ?
						RHIFenceQueryResult::Failed : RHIFenceQueryResult::DeviceLost);
				}
				GGLAB_UNUSED(scheduler.Tick());
				scheduler.DrainReadyWork();
				context.Check(nativeTransfer->GetLastSubmittedFence() == submitted &&
					cpuExecutions == 0 && recordingExecutions == 0 && publication->m_Steps == 0 &&
					publication->m_AbortCount == 0 && completionCount == 0 && !retainedPayload.expired(),
					"Stopped, failed and lost-device schedulers neither submit nor publish or release retained work before quiescence");

				// The fixture has no real GPU; model AppRuntime's verified-quiescence boundary.
				scheduler.Finalize();
				const AssetUploadStatistics statistics = scheduler.GetStatistics();
				context.Check(nativeTransfer->GetLastSubmittedFence() == submitted &&
					completionCount == 1 && completionStatus == AssetUploadStatus::Failed &&
					publication->m_AbortCount == 1 &&
					publication->m_AbortReason == AssetResourcePublicationAbortReason::Shutdown &&
					retainedPayload.expired() && statistics.m_InFlightBytes == 0 &&
					statistics.m_ReadyPayloadBytes == 0 && statistics.m_FailedCount == 1,
					"Quiescent finalization cancels CPU/publication work, fails uploads once and conserves payload accounting without submission");
			}
		}

	}

	void RunAssetUploadSchedulerSelfTests(SelfTestContext& context) noexcept
	{
		RunAssetUploadSchedulerWorkerHandoffTest(context);
		RunStoppedGpuWorkTest(context);
		RunCompletionFailureDuringRecordingTest(context);
		RunResourcePublicationGenerationTest(context);
		RunResourcePublicationDrainTest(context);
	}
}
