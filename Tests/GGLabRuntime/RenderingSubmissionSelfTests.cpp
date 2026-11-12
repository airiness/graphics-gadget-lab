#include "Diagnostics/Builders/RenderGraphSnapshotBuilder.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Diagnostics/Snapshots/AssetSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfileFrameSnapshot.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/RHI/RHISwapChain.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/TransferManager.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/AtmosphereSystem.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/RHI/DX12/DX12Fence.h"
#include "Graphics/RHI/DX12/DX12FencePoint.h"
#include "Graphics/RHI/DX12/DX12TransferContext.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorFreeListAllocator.h"
#include "Graphics/RHI/RHIFrameSubmission.h"
#include "Graphics/RHI/RHIUploadPool.h"
#include "Graphics/RenderFrameOutcome.h"
#include "Graphics/Resource/PersistentTexturePool.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"
#include "RenderingTestServices.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace gglab::rendering_test
{
	class TeardownTestContext final : public RHIContext
	{
	public:
		RHIDevice& GetDevice() noexcept override { std::terminate(); }
		const RHIDevice& GetDevice() const noexcept override { std::terminate(); }
		RHISwapChain& GetSwapChain() noexcept override { std::terminate(); }
		const RHISwapChain& GetSwapChain() const noexcept override { std::terminate(); }
		TransferManager& GetTransferManager() noexcept override { std::terminate(); }
		RHIPipelineSystem& GetPipelineSystem() noexcept override { std::terminate(); }
		GpuProfiler* GetGpuProfiler() noexcept override { return nullptr; }
		RHIFrameBeginResult BeginFrame() noexcept override { return RHIFrameBeginResult::Fatal(); }
		RHIFrameEndResult EndFrame(RHIFrameContext&) noexcept override { return RHIFrameEndResult::Fatal(); }
		RHIFrameEndResult AbortFrame(RHIFrameContext&) noexcept override { return RHIFrameEndResult::Fatal(); }
		bool WaitForFence(RHIQueueType, const RHIFencePoint&) noexcept override { return true; }
		void Resize(uint32_t, uint32_t) noexcept override {}
		RHIGpuWaitResult WaitIdleFor(uint32_t timeoutMilliseconds) noexcept override
		{
			const size_t attempt = m_WaitTimeouts.size();
			m_WaitTimeouts.push_back(timeoutMilliseconds);
			const RHIGpuWaitResult result = attempt < m_WaitResults.size() ?
				m_WaitResults[attempt] :
				RHIGpuWaitResult::Completed;
			m_DeviceLost = m_DeviceLost || attempt == m_LoseDeviceAtAttempt ||
				result == RHIGpuWaitResult::DeviceLost;
			return result;
		}
		void RetireCompletedWork() noexcept override {}
		bool IsPoisoned() const noexcept override { return true; }
		bool IsDeviceLost() const noexcept override { return m_DeviceLost; }
		uint32_t GetFrameSlotCount() const noexcept override { return 2; }

		std::vector<RHIGpuWaitResult> m_WaitResults;
		std::vector<uint32_t> m_WaitTimeouts;
		size_t m_LoseDeviceAtAttempt = std::numeric_limits<size_t>::max();
		bool m_DeviceLost = false;
	};

	void RunDX12TransferCompletionTests(SelfTestContext& context) noexcept
	{
		// An opaque fence identity is enough: injected waits must never dereference it.
		const DX12FencePoint requested(reinterpret_cast<DX12Fence*>(uintptr_t{ 1 }), 7);
		for (const bool wait : { false, true })
		{
			uint32_t waitCalls = 0;
			const DX12FencePoint completion = ResolveDX12TransferCompletion(
				{ requested, false }, wait,
				[&](const DX12FencePoint&)
				{
					++waitCalls;
					return RHIGpuWaitResult::Completed;
				});
			context.Check(!completion.IsValid() && waitCalls == 0 && requested.IsValid(),
				"A failed DX12 transfer signal keeps its retirement identity private and never waits on the unsignaled value");
		}

		uint32_t asyncWaitCalls = 0;
		const DX12FencePoint asynchronous = ResolveDX12TransferCompletion(
			{ requested, true }, false,
			[&](const DX12FencePoint&)
			{
				++asyncWaitCalls;
				return RHIGpuWaitResult::Failed;
			});
		context.Check(asynchronous == requested && asyncWaitCalls == 0,
			"A successfully signaled asynchronous DX12 transfer returns its dependency without a CPU wait");

		for (const RHIGpuWaitResult result : { RHIGpuWaitResult::Completed,
			RHIGpuWaitResult::Timeout, RHIGpuWaitResult::Failed, RHIGpuWaitResult::DeviceLost })
		{
			uint32_t waitCalls = 0;
			const DX12FencePoint completion = ResolveDX12TransferCompletion(
				{ requested, true }, true,
				[&](const DX12FencePoint& fence)
				{
					context.Check(fence == requested,
						"A synchronous DX12 transfer waits only on its successfully signaled point");
					++waitCalls;
					return result;
				});
			context.Check(waitCalls == 1 &&
				completion.IsValid() == (result == RHIGpuWaitResult::Completed),
				"Synchronous DX12 transfer publication requires Completed; timeout, failure and device loss cannot publish");
		}
		context.Check(!ResolveDX12TransferCompletion({ requested, true }, true, {}).IsValid(),
			"A synchronous DX12 transfer without a completion verifier cannot publish");
	}

	void RunCheckedCompletionContractTests(SelfTestContext& context) noexcept
	{
		using enum RHIFenceQueryResult;
		context.Check(ClassifyDX12FenceQuery(DX12DeviceRemovedFenceValue, 5, false) == Failed &&
			ClassifyDX12FenceQuery(DX12DeviceRemovedFenceValue, 5, true) == DeviceLost,
			"The DX12 removal sentinel is device loss only with native confirmation, never completion");
		context.Check(ClassifyDX12FenceQuery(3, 5, false) == Pending &&
			ClassifyDX12FenceQuery(5, 5, false) == Completed &&
			ClassifyDX12FenceQuery(9, 0, false) == Failed,
			"A DX12 in-flight value is Pending and an invalid point is never Completed");
		context.Check(!ClassifyDX12FenceWaitStep(DX12FenceEventWait::Signaled, Pending).has_value() &&
			ClassifyDX12FenceWaitStep(DX12FenceEventWait::Signaled, DeviceLost) ==
				RHIGpuWaitResult::DeviceLost &&
			ClassifyDX12FenceWaitStep(DX12FenceEventWait::Signaled, Failed) ==
				RHIGpuWaitResult::Failed &&
			ClassifyDX12FenceWaitStep(DX12FenceEventWait::TimedOut, Pending) ==
				RHIGpuWaitResult::Timeout &&
			ClassifyDX12FenceWaitStep(DX12FenceEventWait::TimedOut, Completed) ==
				RHIGpuWaitResult::Completed &&
			ClassifyDX12FenceWaitStep(DX12FenceEventWait::Failed, Pending) ==
				RHIGpuWaitResult::Failed,
			"A woken DX12 fence event proves nothing; the following query decides the wait");
		context.Check(ToRHIGpuWaitResult(Pending) == RHIGpuWaitResult::Timeout &&
			ToRHIGpuWaitResult(Failed) == RHIGpuWaitResult::Failed,
			"A pending query is a timeout, never device loss");

		const RHIFencePoint graphicsFence{ RHIFenceHandle{ 1, 1 }, 5 };
		for (const RHIFenceQueryResult result : { Pending, Completed, Failed, DeviceLost })
		{
			RecordingDevice device;
			device.m_FenceQueryResult = result;
			context.Check(device.IsFencePointCompleted(graphicsFence) ==
				(result == Completed || result == DeviceLost) &&
				device.HasCompletionError() == (result == Failed || result == DeviceLost) &&
				device.HasObservedDeviceLoss() == (result == DeviceLost),
				"Routine release queries use checked results and propagate failure without inventing loss");
			device.m_FenceQueryResult = Completed;
			context.Check(device.QueryFencePoint(graphicsFence) ==
				(result == DeviceLost ? DeviceLost : Completed) &&
				device.HasCompletionError() == (result == Failed || result == DeviceLost),
				"Later successful queries cannot clear completion poison or confirmed device loss");
		}
		RecordingDevice invalid;
		context.Check(invalid.IsFencePointCompleted({}) && !invalid.HasCompletionError() &&
			invalid.QueryFencePoint({}) == Failed && !invalid.HasCompletionError(),
			"An absent retirement point is releasable but never authenticates contents, without poisoning recording");
		RecordingDevice misuse;
		misuse.m_FenceQueryResult = Failed;
		misuse.m_ObserveFenceQueryResult = false;
		context.Check(!misuse.IsFencePointCompleted(graphicsFence) &&
			misuse.QueryFencePoint(graphicsFence) == Failed && !misuse.HasCompletionError(),
			"Rejected caller misuse fails release and checked queries without poisoning recording");
		misuse.m_ObserveFenceQueryResult = true;
		misuse.m_FenceQueryResult = DeviceLost;
		GGLAB_UNUSED(misuse.QueryFencePoint(graphicsFence));
		misuse.m_ObserveFenceQueryResult = false;
		misuse.m_FenceQueryResult = Completed;
		context.Check(misuse.QueryFencePoint(graphicsFence) == DeviceLost,
			"Observed device loss stays authoritative over later unobserved backend results");
		RecordingDevice timedOut;
		context.Check(timedOut.ObserveGpuWaitResult(RHIGpuWaitResult::Timeout) ==
			RHIGpuWaitResult::Timeout && !timedOut.HasCompletionError(),
			"A routine wait timeout neither poisons recording nor invents device loss");
		context.Check(timedOut.ObserveGpuWaitResult(RHIGpuWaitResult::Failed) ==
			RHIGpuWaitResult::Failed && timedOut.HasCompletionError() && !timedOut.HasObservedDeviceLoss(),
			"A failed CPU wait poisons recording while retaining the live-device distinction");

		for (const RHIFenceQueryResult result : { Failed, DeviceLost })
		{
			RecordingDevice device;
			device.m_PassConstantsEnabled = true;
			RHIUploadPool uploads(device);
			auto batch = uploads.BeginBatch();
			const std::array<uint32_t, 4> payload{ 1, 2, 3, 4 };
			context.Check(batch->Upload(std::as_bytes(std::span(payload))).IsValid() &&
				batch->Publish() && batch->Submitted(graphicsFence),
				"A retirement-query regression starts with a submitted upload page");
			device.m_FenceQueryResult = result;
			uploads.ReclaimCompleted();
			context.Check(device.HasCompletionError() &&
				uploads.GetPendingPageCount() == (result == Failed ? 1u : 0u),
				"Routine page retirement propagates query errors, retaining failed live-device work and releasing only after native-confirmed loss");
			device.m_FenceQueryResult = Completed;
			uploads.ReclaimCompleted();
		}

		context.Check(RHIFrameEndResult::Fatal().GetSubmissionState() ==
				RHIFrameSubmissionState::NoOutstandingWork &&
			RHIFrameEndResult::Fatal(graphicsFence).GetSubmissionState() ==
				RHIFrameSubmissionState::Fenced &&
			RHIFrameEndResult::Completed(graphicsFence).GetSubmissionState() ==
				RHIFrameSubmissionState::Fenced &&
			RHIFrameEndResult::FatalCompletionUnknown().IsCompletionUnknown() &&
			!RHIFrameEndResult::FatalCompletionUnknown().HasSubmission(),
			"Frame results state whether frame work may still execute");

		const auto quiesce = [](TeardownTestContext& rhi) noexcept
			{
				return QuiesceRHIForTeardown(
					rhi, { .m_AttemptCount = 3, .m_AttemptTimeoutMilliseconds = 7 });
			};
		TeardownTestContext retried;
		retried.m_WaitResults = { RHIGpuWaitResult::Timeout, RHIGpuWaitResult::Timeout };
		context.Check(quiesce(retried) == RHITeardownState::Quiesced &&
			retried.m_WaitTimeouts == std::vector<uint32_t>({ 7, 7, 7 }) && !retried.m_DeviceLost,
			"Timed-out teardown waits on a live device release nothing until a later wait completes");
		TeardownTestContext exhausted;
		exhausted.m_WaitResults.assign(3, RHIGpuWaitResult::Timeout);
		TeardownTestContext failed;
		failed.m_WaitResults.assign(3, RHIGpuWaitResult::Failed);
		context.Check(quiesce(exhausted) == RHITeardownState::Unverified &&
			quiesce(failed) == RHITeardownState::Unverified && exhausted.m_WaitTimeouts.size() == 3 &&
			!exhausted.m_DeviceLost && !failed.m_DeviceLost,
			"An exhausted bound leaks on a live device; timeouts and failures never imply loss");
		TeardownTestContext lost;
		lost.m_WaitResults = { RHIGpuWaitResult::Timeout };
		lost.m_LoseDeviceAtAttempt = 0;
		TeardownTestContext alreadyLost;
		alreadyLost.m_DeviceLost = true;
		context.Check(quiesce(lost) == RHITeardownState::DeviceLost && lost.m_WaitTimeouts.size() == 1 &&
			quiesce(alreadyLost) == RHITeardownState::DeviceLost && alreadyLost.m_WaitTimeouts.empty(),
			"Teardown reads the device state after every wait and never waits after confirmed loss");
	}

	void RunDependentFrameSubmissionTests(SelfTestContext& context) noexcept
	{
		RecordingDevice device;
		device.m_PassConstantsEnabled = true;
		device.m_UseControlledFenceCompletion = true;
		RHIUploadPool pool(device);
		const RHIFencePoint computeFence{ RHIFenceHandle{ 2, 1 }, 4 };
		const RHIFencePoint graphicsFence{ RHIFenceHandle{ 1, 1 }, 5 };
		const std::array<uint32_t, 4> payload{ 1, 2, 3, 4 };
		for (uint32_t scenario = 0; scenario < 3; ++scenario)
		{
			device.m_CompletedFenceValue = 0;
			auto batch = pool.BeginBatch();
			context.Check(batch->Upload(std::as_bytes(std::span(payload))).IsValid() && batch->Publish(),
				"A dependent-stream fault case has published upload data");
			std::vector<uint32_t> events;
			std::array<RHISubmittedStream, 2> streams{};
			streams[0] = { RHISubmitEffect::Accepted, scenario == 2 ? RHIFencePoint{} : computeFence };
			streams[1] = SubmitDependentFrameStream(streams[0],
				[&](const RHIFencePoint& fence)
				{
					events.push_back(fence == computeFence ? 1u : 0u);
					return scenario == 0;
				},
				[&]() -> RHISubmittedStream
				{
					events.push_back(2);
					return { RHISubmitEffect::Accepted, graphicsFence };
				});
			uint32_t cpuWaits = 0;
			const RHISubmissionVerification verification{
				.m_WaitForFence = [&](const RHIFencePoint&)
					{
						++cpuWaits;
						return RHIGpuWaitResult::Completed;
					},
				.m_WaitIdle = []() { return RHIGpuWaitResult::Timeout; },
			};
			const auto resolution = ResolveFrameSubmission(streams, *batch, verification);
			if (scenario == 0)
			{
				device.m_CompletedFenceValue = computeFence.m_Value;
				pool.ReclaimCompleted();
				context.Check(events == std::vector<uint32_t>({ 1, 2 }) &&
					streams[0].m_JoinedByNextStream && resolution.m_Fence == graphicsFence &&
					pool.GetPendingPageCount() == 1 && cpuWaits == 0,
					"Graphics submits only after the confirmed GPU join and retires after graphics");
				device.m_CompletedFenceValue = graphicsFence.m_Value;
			}
			else if (scenario == 1)
			{
				device.m_CompletedFenceValue = computeFence.m_Value - 1;
				pool.ReclaimCompleted();
				context.Check(events == std::vector<uint32_t>({ 1 }) &&
					streams[1].m_Effect == RHISubmitEffect::NoEffect &&
					resolution.m_State == RHIFrameSubmissionState::Fenced &&
					resolution.m_Fence == computeFence && pool.GetPendingPageCount() == 1 && cpuWaits == 0,
					"A failed queue join never submits graphics or performs a late CPU repair");
				device.m_CompletedFenceValue = computeFence.m_Value;
			}
			else
			{
				device.m_CompletedFenceValue = graphicsFence.m_Value;
				pool.ReclaimCompleted();
				context.Check(events.empty() && streams[1].m_Effect == RHISubmitEffect::NoEffect &&
					resolution.m_State == RHIFrameSubmissionState::CompletionUnknown &&
					!resolution.m_DeviceLost && pool.GetQuarantinedPageCount() == 1 && cpuWaits == 0,
					"A failed compute signal submits no graphics and retains uploads until verified idle");
				pool.ReleaseQuarantinedAfterVerifiedIdle();
			}
			pool.ReclaimCompleted();
			context.Check(pool.GetPendingPageCount() == 0 && pool.GetQuarantinedPageCount() == 0,
				"Dependent-stream uploads retire only through the selected completion path");
		}

		// A confirmed join whose dependent stream had no effect left no executed waiter. A
		// later independent stream's fence covers the prerequisite only after a CPU wait.
		{
			device.m_CompletedFenceValue = 0;
			const RHIFencePoint independentFence{ RHIFenceHandle{ 1, 1 }, 6 };
			auto batch = pool.BeginBatch();
			const bool published =
				batch->Upload(std::as_bytes(std::span(payload))).IsValid() && batch->Publish();
			std::array<RHISubmittedStream, 3> streams{};
			streams[0] = { RHISubmitEffect::Accepted, computeFence };
			streams[1] = SubmitDependentFrameStream(streams[0],
				[](const RHIFencePoint&) { return true; },
				[]() -> RHISubmittedStream { return {}; });
			streams[2] = { RHISubmitEffect::Accepted, independentFence };
			std::vector<RHIFencePoint> cpuWaits;
			const RHISubmissionVerification verification{
				.m_WaitForFence = [&](const RHIFencePoint& fence)
					{
						cpuWaits.push_back(fence);
						return RHIGpuWaitResult::Completed;
					},
				.m_WaitIdle = []() { return RHIGpuWaitResult::Timeout; },
			};
			const auto resolution = ResolveFrameSubmission(streams, *batch, verification);
			context.Check(published && !streams[0].m_JoinedByNextStream &&
				streams[1].m_Effect == RHISubmitEffect::NoEffect &&
				cpuWaits == std::vector<RHIFencePoint>({ computeFence }) &&
				resolution.m_State == RHIFrameSubmissionState::Fenced &&
				resolution.m_Fence == independentFence,
				"A dependent stream without effect does not count as a join of its prerequisite");
			device.m_CompletedFenceValue = independentFence.m_Value;
			pool.ReclaimCompleted();
		}
	}

	void RunPassProfileIdentityContractTests(SelfTestContext& context) noexcept
	{
		RecordingDevice device;
		TransientResourcePool pool(&device);
		RecordingGraphicsCommandContext graphics;
		RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
		struct Data {};
		const auto culled = [](RenderGraph::RGBuilder&, Data&) {};
		graph.AddPass<Data>("Repeated", culled);
		graph.AddTrivialSideEffectPass("Repeated", [](RGExecuteContext&) {});
		graph.AddTrivialSideEffectPass("Separator", [](RGExecuteContext&) {});
		graph.AddPass<Data>("Repeated", culled);
		graph.AddTrivialSideEffectPass("Repeated", [](RGExecuteContext&) {});

		RGSnapshot declared;
		BuildRenderGraphSnapshot(graph, declared);
		context.Check(declared.m_Passes.size() == 5 &&
			declared.m_Passes[0].m_Occurrence == 1 &&
			declared.m_Passes[1].m_Occurrence == 2 &&
			declared.m_Passes[2].m_Occurrence == 1 &&
			declared.m_Passes[3].m_Occurrence == 3 &&
			declared.m_Passes[4].m_Occurrence == 4,
			"Pass identity exists before compilation and counts every same-name declaration");
		const bool compiled = graph.Compile();
		context.Check(compiled, "Same-name profiling fixture compiles with culled declarations");
		if (!compiled)
		{
			return;
		}
		RGDeferredCommitList commits;
		RGExecuteContext execute(commits, { .m_GraphicsCommandContext = &graphics });
		graph.Execute(execute);
		commits.Discard();
		RGSnapshot snapshot;
		BuildRenderGraphSnapshot(graph, snapshot);
		const auto& samples = graphics.m_Profile.m_Samples;
		context.Check(snapshot.m_Passes[0].m_Culled && snapshot.m_Passes[3].m_Culled &&
			samples.size() == 3 &&
			samples[0].GetScope() == GpuProfileScope{ "Repeated", GpuProfileScopeKind::RenderGraphPass, 2 } &&
			samples[1].GetScope() == GpuProfileScope{ "Separator", GpuProfileScopeKind::RenderGraphPass, 1 } &&
			samples[2].GetScope() == GpuProfileScope{ "Repeated", GpuProfileScopeKind::RenderGraphPass, 4 } &&
			graphics.m_BeginProfileCount == graphics.m_EndProfileCount,
			"Non-contiguous live passes retain declaration identity through execution and scope recording");
		bool matchesGraph = true;
		for (const auto& sample : samples)
		{
			matchesGraph &= std::ranges::any_of(snapshot.m_Passes,
				[&sample](const RGSnapshotPassInfo& pass)
				{
					return !pass.m_Culled && pass.m_Name == sample.m_Name &&
						pass.m_Occurrence == sample.m_Occurrence;
				});
		}
		context.Check(matchesGraph && snapshot.m_DuplicatePassNames ==
			std::vector<std::string>{ "Repeated" },
			"Graph diagnostics and GPU scopes consume the same identity without recounting executed passes");

		AccumulateGpuProfileSample(graphics.m_Profile, { "Repeated" }, 3.0);
		AccumulateGpuProfileSample(graphics.m_Profile, { "IBL.Bake" }, 4.0);
		AccumulateGpuProfileSample(graphics.m_Profile, { "Repeated" }, 5.0);
		AccumulateGpuProfileSample(graphics.m_Profile, { "Upload" }, 6.0);
		context.Check(samples.size() == 6 && samples[0].m_Milliseconds == 1.0 &&
			samples[2].m_Milliseconds == 1.0 && samples[3].m_Milliseconds == 8.0 &&
			samples[3].m_CallCount == 2 && samples[3].GetScope() == GpuProfileScope{ "Repeated" } &&
			samples[4].GetScope() == GpuProfileScope{ "IBL.Bake" } &&
			samples[5].GetScope() == GpuProfileScope{ "Upload" },
			"Native profiler accumulation separates pass occurrences and same-name other scopes");
		context.Check(graph.Compile(), "Recompilation preserves the declaration-owned pass identities");
		RGSnapshot recompiled;
		BuildRenderGraphSnapshot(graph, recompiled);
		context.Check(recompiled.m_Passes[4].m_Occurrence == declared.m_Passes[4].m_Occurrence,
			"Recompilation never advances occurrence counters");
	}

	void RunDeferredCommitContractTests(SelfTestContext& context) noexcept
	{
		struct ThrowingCommit
		{
			void operator()(const RGCommitContext&) {}
		};
		static_assert(!std::is_default_constructible_v<RGExecuteContext>);
		static_assert(!std::is_constructible_v<RGExecuteContext, RGBackendExecuteContext>);
		static_assert(std::is_nothrow_constructible_v<RGExecuteContext, RGDeferredCommitList&>);
		static_assert(!std::is_copy_constructible_v<RGDeferredCommit>);
		static_assert(std::is_nothrow_move_constructible_v<RGDeferredCommit>);
		static_assert(!std::is_constructible_v<RGDeferredCommit, ThrowingCommit>);
		struct Payload
		{
			uint32_t& m_Destructions;
			~Payload() noexcept { ++m_Destructions; }
		};
		const RGCommitContext submitted{ 71, { RHIFenceHandle{ 1, 1 }, 42 } };
		{
			RGDeferredCommitList commits;
			RGExecuteContext execute(commits);
			std::vector<uint32_t> order;
			uint32_t destroyed = 0;
			bool identityMatches = true;
			for (uint32_t index : { 1u, 2u })
			{
				execute.DeferCommit([&, index, payload = std::make_unique<Payload>(destroyed)]
					(const RGCommitContext& commit) noexcept
					{
						identityMatches &= commit.m_FrameSerial == submitted.m_FrameSerial &&
							commit.m_Fence == submitted.m_Fence;
						order.push_back(index);
					});
			}
			context.Check(order.empty() && destroyed == 0,
				"Enqueue retains move-only CPU captures without committing during recording");
			commits.Seal();
			commits.CommitAll(submitted);
			commits.CommitAll(submitted);
			commits.Discard();
			context.Check(order == std::vector<uint32_t>({ 1, 2 }) && identityMatches &&
				destroyed == 2 && !commits.HasFailed(),
				"Deferred commits run once in enqueue order with the real serial/fence and release CPU captures");
		}
		{
			RGDeferredCommitList commits;
			RGExecuteContext execute(commits);
			uint32_t ran = 0;
			uint32_t destroyed = 0;
			execute.DeferCommit([&, payload = std::make_unique<Payload>(destroyed)]
				(const RGCommitContext&) noexcept { ++ran; });
			commits.Discard();
			commits.CommitAll(submitted);
			context.Check(ran == 0 && destroyed == 1 && !commits.HasFailed(),
				"Discard drops unsubmitted CPU captures without invoking them or allowing a later commit");
		}
		{
			RGDeferredCommitList commits;
			RGExecuteContext execute(commits);
			uint32_t ran = 0;
			execute.DeferCommit([&](const RGCommitContext&) noexcept { ++ran; });
			commits.CommitAll({ 71, {} });
			commits.CommitAll(submitted);
			context.Check(commits.HasFailed() && ran == 0,
				"CommitAll rejects an invalid fence in every configuration and never invokes that batch");
			RGDeferredCommitList empty;
			empty.CommitAll({});
			context.Check(empty.HasFailed(), "Even an empty commit list requires a valid submission fence");
		}
		{
			RGDeferredCommitList commits;
			RGExecuteContext execute(commits);
			uint32_t ran = 0;
			uint32_t destroyed = 0;
			execute.DeferCommit([&](const RGCommitContext&) noexcept { ++ran; });
			commits.Seal();
			execute.DeferCommit([&, payload = std::make_unique<Payload>(destroyed)]
				(const RGCommitContext&) noexcept { ++ran; });
			commits.CommitAll(submitted);
			context.Check(commits.HasFailed() && ran == 0 && destroyed == 1,
				"A sealed list rejects and releases a late enqueue, and its failed batch cannot commit");
		}
		{
			RGDeferredCommitList commits;
			RGExecuteContext execute(commits);
			std::vector<uint32_t> order;
			// Deliberately retain a still-live context to exercise the reentry guard.
			execute.DeferCommit([&](const RGCommitContext&) noexcept
				{
					order.push_back(1);
					execute.DeferCommit([&](const RGCommitContext&) noexcept { order.push_back(99); });
				});
			execute.DeferCommit([&](const RGCommitContext&) noexcept { order.push_back(2); });
			commits.Seal();
			commits.CommitAll(submitted);
			commits.CommitAll(submitted);
			context.Check(commits.HasFailed() && order == std::vector<uint32_t>({ 1, 2 }),
				"Settlement rejects enqueue without invalidating iteration or skipping/repeating original commits");
		}
		{
			RecordingDevice device;
			TransientResourcePool pool(&device);
			RecordingGraphicsCommandContext graphics;
			RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
			RGDeferredCommitList commits;
			RGExecuteContext execute(commits, { .m_GraphicsCommandContext = &graphics });
			std::vector<uint32_t> order;
			graph.AddTrivialSideEffectPass("Commit.First", [&](RGExecuteContext& execution)
				{
					execution.DeferCommit([&](const RGCommitContext&) noexcept { order.push_back(1); });
				});
			graph.AddPass<StorageAccessPassData>("Commit.Culled",
				[](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.Write(builder.CreateBuffer("Commit.Unused", { .m_SizeInBytes = 4 }),
						RGBufferAccess::StorageWrite);
				},
				[&](RGExecuteContext& execution, StorageAccessPassData&)
				{
					execution.DeferCommit([&](const RGCommitContext&) noexcept { order.push_back(99); });
				});
			graph.AddTrivialSideEffectPass("Commit.Last", [&](RGExecuteContext& execution)
				{
					execution.DeferCommit([&](const RGCommitContext&) noexcept { order.push_back(2); });
				});
			const bool compiled = graph.Compile();
			context.Check(compiled, "Deferred-commit liveness fixture compiles");
			if (compiled)
			{
				graph.Execute(execute);
				commits.Seal();
				context.Check(order.empty(), "Graph execution only enqueues its CPU state changes");
				commits.CommitAll(submitted);
				context.Check(order == std::vector<uint32_t>({ 1, 2 }),
					"Culled passes enqueue nothing and live passes commit in execution order");
				graph.Retire(submitted.m_Fence);
			}
		}
	}

	void RunRenderFrameOutcomeContractTests(SelfTestContext& context) noexcept
	{
		const RHIFencePoint fence{ RHIFenceHandle{ 1, 1 }, 42 };
		struct Case
		{
			RenderFrameEndPath m_Path;
			RHIFrameEndResult m_Result;
			RenderFrameOutcomeKind m_Expected;
			std::string_view m_Description;
		};
		const std::array cases
		{
			Case{ RenderFrameEndPath::EndFrame, RHIFrameEndResult::Completed(fence), RenderFrameOutcomeKind::Submitted, "A completed EndFrame is Submitted with its frame fence" },
			Case{ RenderFrameEndPath::EndFrame, RHIFrameEndResult::Fatal(fence), RenderFrameOutcomeKind::Failed, "A fenced EndFrame failure stays Failed even after full submission" },
			Case{ RenderFrameEndPath::EndFrame, RHIFrameEndResult::Fatal(), RenderFrameOutcomeKind::Failed, "An unfenced EndFrame failure stays Failed even when work is already idle" },
			Case{ RenderFrameEndPath::AbortActiveFrame, RHIFrameEndResult::Fatal(fence), RenderFrameOutcomeKind::Discarded, "A fenced Renderer abort is Discarded with its retention fence" },
			Case{ RenderFrameEndPath::AbortActiveFrame, RHIFrameEndResult::Fatal(), RenderFrameOutcomeKind::Discarded, "An unfenced Renderer abort is Discarded" },
			Case{ RenderFrameEndPath::EndFrame, RHIFrameEndResult::FatalCompletionUnknown(), RenderFrameOutcomeKind::CompletionUnknown, "Unknown completion overrides the EndFrame path" },
			Case{ RenderFrameEndPath::AbortActiveFrame, RHIFrameEndResult::FatalCompletionUnknown(), RenderFrameOutcomeKind::CompletionUnknown, "Unknown completion overrides the Renderer abort path" },
		};
		uint64_t serial = 100;
		for (const Case& test : cases)
		{
			const auto outcome = ResolveRenderFrameOutcome(test.m_Path, ++serial, test.m_Result);
			context.Check(outcome.m_Kind == test.m_Expected && outcome.m_FrameSerial == serial &&
				outcome.m_Fence == test.m_Result.GetSubmittedFence(), test.m_Description);
		}
		const auto withoutRHI = ResolveRenderFrameOutcome(RenderFrameEndPath::AbortActiveFrame, ++serial);
		context.Check(withoutRHI.m_Kind == RenderFrameOutcomeKind::Discarded &&
			withoutRHI.m_FrameSerial == serial && !withoutRHI.m_Fence.IsValid(),
			"A Renderer abort without an RHI frame is Discarded without a fence");
	}

	void RunFrameSubmissionContractTests(SelfTestContext& context) noexcept
	{
		RunDependentFrameSubmissionTests(context);
		RecordingDevice device;
		device.m_PassConstantsEnabled = true;
		device.m_UseControlledFenceCompletion = true;
		device.m_CompletedFenceValue = 0;
		RHIUploadPool pool(device);
		const std::array<uint32_t, 4> payload{ 1, 2, 3, 4 };
		bool batchesReady = true;
		const auto beginPublishedBatch = [&]()
			{
				auto batch = pool.BeginBatch();
				batchesReady &= batch->Upload(std::as_bytes(std::span(payload))).IsValid() &&
					batch->Publish();
				return batch;
			};
		RHIGpuWaitResult fenceWaitResult = RHIGpuWaitResult::Completed;
		RHIGpuWaitResult idleWaitResult = RHIGpuWaitResult::Completed;
		uint32_t fenceWaits = 0;
		uint32_t idleWaits = 0;
		const RHISubmissionVerification verification{
			.m_WaitForFence = [&](const RHIFencePoint&)
				{
					++fenceWaits;
					return fenceWaitResult;
				},
			.m_WaitIdle = [&]()
				{
					++idleWaits;
					if (idleWaitResult == RHIGpuWaitResult::Completed)
					{
						pool.ReleaseQuarantinedAfterVerifiedIdle();
					}
					return idleWaitResult;
				},
		};
		const RHIFencePoint computeFence{ RHIFenceHandle{ 2, 1 }, 4 };
		const RHIFencePoint graphicsFence{ RHIFenceHandle{ 1, 1 }, 5 };
		const auto resolve = [&](RHIUploadBatch& batch,
			std::initializer_list<RHISubmittedStream> streams)
			{
				return ResolveFrameSubmission(
					std::span(streams.begin(), streams.size()), batch, verification);
			};
		using enum RHISubmitEffect;

		{
			auto batch = beginPublishedBatch();
			const auto resolution = resolve(*batch, { { NoEffect, {}, false } });
			context.Check(resolution.m_State == RHIFrameSubmissionState::NoOutstandingWork &&
				!batch->HasAcceptedSubmission() && pool.GetPendingPageCount() == 0 &&
				pool.GetQuarantinedPageCount() == 0 && idleWaits == 0,
				"A submit without effect cancels the batch and reports no outstanding work");
		}
		{
			auto batch = beginPublishedBatch();
			const auto resolution = resolve(*batch,
				{ { Accepted, computeFence, true }, { Accepted, graphicsFence, false } });
			device.m_CompletedFenceValue = computeFence.m_Value;
			pool.ReclaimCompleted();
			const bool heldPastCompute = pool.GetPendingPageCount() == 1;
			device.m_CompletedFenceValue = graphicsFence.m_Value;
			pool.ReclaimCompleted();
			context.Check(resolution.m_State == RHIFrameSubmissionState::Fenced &&
				resolution.m_Fence == graphicsFence && fenceWaits == 0 && heldPastCompute &&
				pool.GetPendingPageCount() == 0,
				"A confirmed join retires the batch on the graphics fence, not the compute fence");
		}
		{
			device.m_CompletedFenceValue = 0;
			auto batch = beginPublishedBatch();
			const auto resolution = resolve(*batch,
				{ { Accepted, computeFence, false }, { NoEffect, {}, false } });
			device.m_CompletedFenceValue = computeFence.m_Value - 1;
			pool.ReclaimCompleted();
			const bool heldBeforeCompute = pool.GetPendingPageCount() == 1;
			device.m_CompletedFenceValue = computeFence.m_Value;
			pool.ReclaimCompleted();
			context.Check(resolution.m_State == RHIFrameSubmissionState::Fenced &&
				resolution.m_Fence == computeFence && batch->HasAcceptedSubmission() &&
				heldBeforeCompute && pool.GetPendingPageCount() == 0,
				"Compute accepted before a graphics list without effect retires on the compute fence");
		}
		{
			device.m_CompletedFenceValue = 0;
			idleWaitResult = RHIGpuWaitResult::Timeout;
			auto batch = beginPublishedBatch();
			const auto resolution = resolve(*batch,
				{ { Accepted, computeFence, true }, { Accepted, {}, false } });
			device.m_CompletedFenceValue = 100;
			pool.ReclaimCompleted();
			const size_t pagesBefore = pool.GetPageCount();
			auto next = pool.BeginBatch();
			const bool nextUploaded = next->Upload(std::as_bytes(std::span(payload))).IsValid();
			const bool quarantineSkipped = pool.GetPageCount() == pagesBefore + 1;
			next->Cancel();
			context.Check(resolution.m_State == RHIFrameSubmissionState::CompletionUnknown &&
				!resolution.m_DeviceLost && pool.GetQuarantinedPageCount() == 1 && nextUploaded &&
				quarantineSkipped,
				"A graphics stream without a fence quarantines pages past the compute fence");
			pool.ReleaseQuarantinedAfterVerifiedIdle();
			context.Check(pool.GetQuarantinedPageCount() == 0,
				"Only verified idle releases quarantined pages");
		}
		{
			idleWaitResult = RHIGpuWaitResult::Completed;
			const uint32_t idleWaitsBefore = idleWaits;
			auto batch = beginPublishedBatch();
			const auto resolution = resolve(*batch,
				{ { Accepted, computeFence, true }, { Accepted, {}, false } });
			context.Check(resolution.m_State == RHIFrameSubmissionState::NoOutstandingWork &&
				idleWaits == idleWaitsBefore + 1 && pool.GetQuarantinedPageCount() == 0,
				"A verified idle wait resolves an unfenced stream to no outstanding work");
		}
		{
			fenceWaitResult = RHIGpuWaitResult::Timeout;
			idleWaitResult = RHIGpuWaitResult::Timeout;
			auto batch = beginPublishedBatch();
			const auto resolution = resolve(*batch,
				{ { Accepted, computeFence, false }, { Accepted, graphicsFence, false } });
			context.Check(resolution.m_State == RHIFrameSubmissionState::CompletionUnknown &&
				!resolution.m_DeviceLost && fenceWaits == 1 && pool.GetQuarantinedPageCount() == 1,
				"A failed queue wait is no join, and a timed-out verification leaves the device live");
			pool.ReleaseQuarantinedAfterVerifiedIdle();
			fenceWaitResult = RHIGpuWaitResult::Completed;
			device.m_CompletedFenceValue = 0;
			auto verified = beginPublishedBatch();
			const auto joined = resolve(*verified,
				{ { Accepted, computeFence, false }, { Accepted, graphicsFence, false } });
			context.Check(joined.m_State == RHIFrameSubmissionState::Fenced &&
				joined.m_Fence == graphicsFence && fenceWaits == 2,
				"A completed CPU wait covers an unjoined earlier stream");
			device.m_CompletedFenceValue = graphicsFence.m_Value;
			pool.ReclaimCompleted();
		}
		{
			idleWaitResult = RHIGpuWaitResult::DeviceLost;
			auto batch = beginPublishedBatch();
			const auto resolution = resolve(*batch, { { Accepted, {}, false } });
			context.Check(resolution.m_State == RHIFrameSubmissionState::CompletionUnknown &&
				resolution.m_DeviceLost && batch->HasAcceptedSubmission() &&
				pool.GetQuarantinedPageCount() == 1,
				"An accepted submit lost with the device stays quarantined for device-loss teardown");
			pool.ReleaseQuarantinedAfterDeviceLoss();
			context.Check(pool.GetQuarantinedPageCount() == 0,
				"Device-loss teardown releases the quarantined pages for destruction");
		}
		context.Check(batchesReady, "Every fault-injection batch uploaded and published its payload");

		RecordingDevice atmosphereDevice;
		atmosphereDevice.m_TextureViewsSupported = true;
		atmosphereDevice.m_UseControlledFenceCompletion = true;
		PersistentTexturePool texturePool(&atmosphereDevice);
		AtmosphereSystem atmosphere(&atmosphereDevice, &texturePool);
		const bool begun = atmosphere.Begin(AtmosphereGPU{}, { 1, 1, 1 });
		for (uint32_t index = 0; begun && index < 3; ++index)
		{
			atmosphere.NotifyExecuted(index);
		}
		atmosphere.RetainAfterUnknownCompletion();
		atmosphereDevice.m_CompletedFenceValue = 100;
		texturePool.Tick();
		context.Check(begun && texturePool.GetDiagnostics().m_ActiveTextureCount == 3 &&
			texturePool.GetDiagnostics().m_PendingRetirementTextureCount == 0 &&
			atmosphereDevice.m_AtmosphereDestroyedBufferCount == 0 &&
			!atmosphere.GetDiagnostics().m_Available && !atmosphere.IsInitialized(0),
			"An unknown completion releases no atmosphere LUT and certifies nothing");
		atmosphere.Shutdown();
		texturePool.Tick();
		context.Check(texturePool.GetDiagnostics().m_ActiveTextureCount == 0 &&
			atmosphereDevice.m_AtmosphereDestroyedBufferCount == 0,
			"Teardown releases the retained atmosphere set through its owners");
	}
}

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingSubmissionSelfTests(SelfTestContext& context) noexcept
	{
		RunCheckedCompletionContractTests(context);
		RunDX12TransferCompletionTests(context);
		RunFrameSubmissionContractTests(context);
		RunRenderFrameOutcomeContractTests(context);
		RunDeferredCommitContractTests(context);
		RunPassProfileIdentityContractTests(context);
	}
}
