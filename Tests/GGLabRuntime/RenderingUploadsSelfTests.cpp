#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabRuntime/Diagnostics/Snapshots/AssetSnapshot.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHIUpload.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/RHI/RHIUploadPool.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"
#include "RenderingTestServices.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>

namespace gglab::rendering_test
{
	void RunSubmissionRetentionContractTests(SelfTestContext& context) noexcept
	{
		for (uint32_t scenario = 0; scenario < 5; ++scenario)
		{
			RecordingDevice device;
			device.m_UseControlledFenceCompletion = true;
			RHIUploadPool pool(device);
			auto batch = pool.BeginBatch();
			auto original = std::make_shared<uint32_t>(73);
			std::weak_ptr<uint32_t> originalWeak = original;
			context.Check(batch->RetainForSubmission(std::move(original)),
				"A recording batch accepts native backing before publication");
			if (scenario < 3)
			{
				context.Check(batch->Publish(), "Native retention publication succeeds before a late-lease probe");
				if (scenario == 1)
				{
					batch->RecordAcceptedSubmission();
					context.Check(batch->Submitted({ { 0, 1 }, 7 }),
						"A submitted retention probe transfers its original backing to the fence owner");
				}
				else if (scenario == 2)
				{
					batch->RecordAcceptedSubmission();
					batch->SubmittedWithoutCompletionFence();
				}
			}
			else if (scenario == 3)
			{
				batch->Cancel();
			}
			else
			{
				batch->Fail();
			}
			auto late = std::make_shared<uint32_t>(91);
			std::weak_ptr<uint32_t> lateWeak = late;
			context.Check(!batch->RetainForSubmission(std::move(late)) &&
				lateWeak.expired() && batch->HasFailed(),
				"Published, submitted, quarantined, cancelled and failed batches reject new native leases");
			context.Check(originalWeak.expired() == (scenario == 3),
				"Rejecting a late lease preserves the original backing's existing retention state");
			if (scenario == 1)
			{
				batch.reset();
				pool.ReclaimCompleted();
				context.Check(!originalWeak.expired(),
					"Late-lease rejection does not release submitted backing before its fence");
				device.m_CompletedFenceValue = 7;
				pool.ReclaimCompleted();
			}
			else if (scenario == 2)
			{
				batch.reset();
				pool.ReleaseQuarantinedAfterVerifiedIdle();
			}
			else if (scenario != 3)
			{
				batch->Cancel();
			}
			context.Check(originalWeak.expired(),
				"The original backing still releases through cancellation, completion or verified idle");
		}
		RecordingDevice device;
		RHIUploadPool pool(device);
		auto batch = pool.BeginBatch();
		context.Check(!batch->RetainForSubmission({}) && batch->HasFailed(),
			"A null owner-thread native lease fails the recording");
		batch->Cancel();
	}

	void RunUploadStorageContractTests(SelfTestContext& context) noexcept
	{
		for (uint32_t scenario = 0; scenario < 4; ++scenario)
		{
			RecordingDevice retentionDevice;
			retentionDevice.m_UseControlledFenceCompletion = true;
			RHIUploadPool retentionPool(retentionDevice);
			auto retainedBatch = retentionPool.BeginBatch();
			RHIUploadWriter retentionWriter;
			retainedBatch->Attach(retentionWriter);
			auto nativeBacking = std::make_shared<uint32_t>(73);
			std::weak_ptr<uint32_t> weakBacking = nativeBacking;
			context.Check(retentionWriter.RetainForSubmission(nativeBacking),
				"A recording batch pins native backing before any stream can be submitted");
			nativeBacking.reset();
			retainedBatch->Detach(retentionWriter);
			if (scenario == 0)
			{
				retainedBatch->Cancel();
				context.Check(weakBacking.expired(), "Cancellation without accepted work releases native backing immediately");
				continue;
			}
			context.Check(retainedBatch->Publish(), "Submission backing does not require an upload-page allocation");
			retainedBatch->RecordAcceptedSubmission();
			if (scenario == 1)
			{
				context.Check(retainedBatch->Submitted({ { 0, 1 }, 7 }), "Fenced backing joins the complete submission point");
				retainedBatch.reset();
				retentionDevice.m_CompletedFenceValue = 6;
				retentionPool.ReclaimCompleted();
				context.Check(!weakBacking.expired(), "Native backing survives batch destruction and an earlier fence value");
				retentionDevice.m_CompletedFenceValue = 7;
				retentionPool.ReclaimCompleted();
				context.Check(weakBacking.expired(), "Verified completion releases the submission's native backing");
			}
			else
			{
				retainedBatch->SubmittedWithoutCompletionFence();
				retainedBatch.reset();
				retentionDevice.m_UseControlledFenceCompletion = false;
				retentionPool.ReclaimCompleted();
				context.Check(!weakBacking.expired(), "Completion-unknown backing cannot be reclaimed by unrelated completed fences");
				if (scenario == 2)
				{
					retentionPool.ReleaseQuarantinedAfterVerifiedIdle();
				}
				else
				{
					retentionPool.ReleaseQuarantinedAfterDeviceLoss();
				}
				context.Check(weakBacking.expired(), "Verified idle or confirmed device loss ends native-backing quarantine");
			}
		}
		RecordingDevice device;
		device.m_PassConstantsEnabled = true;
		device.m_UseControlledFenceCompletion = true;
		device.m_UploadLimits = { 256, 4096, 512, 8192 };
		RHIUploadPool pool(device, { .m_PageSizeInBytes = 2048 });
		RHIUploadWriter writer;
		RecordingGraphicsCommandContext graphics;
		RecordingComputeCommandContext compute;
		const std::array<uint32_t, 2> constants{ 7, 11 };
		const std::array<std::array<uint32_t, 3>, 2> elements{ { { 1, 2, 3 }, { 4, 5, 6 } } };
		auto batch = pool.BeginBatch();
		batch->Attach(writer);
		graphics.BeginRecordingScope(writer);
		compute.BeginRecordingScope(writer);
		const auto frameUploads = writer.GetFrameUploads();
		const auto first = frameUploads.UploadConstants(constants);
		std::fill(device.m_PassConstantStorage[0].begin() + 256,
			device.m_PassConstantStorage[0].end(), std::byte{ 0xcd });
		const auto structured = frameUploads.UploadStructured(std::span(elements));
		const auto last = frameUploads.UploadConstants(constants);
		const auto& bytes = device.m_PassConstantStorage[0];
		const auto paddingIsZero = [&](size_t begin, size_t end)
			{
				return std::all_of(bytes.begin() + begin, bytes.begin() + end,
					[](std::byte value) { return value == std::byte{}; });
			};
		context.Check(first.IsValid() && structured.IsValid() && last.IsValid() &&
			structured.m_Buffer == first.m_Buffer && structured.m_OffsetInBytes == 512 &&
			structured.m_StrideInBytes == 12 && structured.m_SizeInBytes == sizeof(elements) &&
			last.m_OffsetInBytes == 1024 && paddingIsZero(256, 512) && paddingIsZero(536, 1024) &&
			std::memcmp(bytes.data() + 512, elements.data(), sizeof(elements)) == 0,
			"Mixed uploads align each slice independently and zero both alignment gaps and padding");
		context.Check(graphics.SetReadOnlyBuffer(3, structured) && compute.SetReadOnlyBuffer(3, structured) &&
			writer.Owns(structured) && device.IsUploadStorage(first.m_Buffer) &&
			Test(device.m_UploadBufferDescs[0].m_Usage, RHIBufferUsage::Constant | RHIBufferUsage::Structured),
			"Graphics and compute share one identity-carrying structured slice on marked mixed-use storage");
		context.Check(IsConstantBufferBindingInRange(first, 2048, device.m_UploadLimits) &&
			IsReadOnlyBufferBindingInRange(structured, 2048, device.m_UploadLimits) &&
			!IsReadOnlyBufferBindingInRange({ structured.m_Buffer, UINT64_MAX - 511, 1024, 4 },
				UINT64_MAX, device.m_UploadLimits) &&
			!IsReadOnlyBufferBindingInRange({ structured.m_Buffer, 1, 24, 12 }, 2048, device.m_UploadLimits) &&
			!IsReadOnlyBufferBindingInRange({ structured.m_Buffer, 512, 25, 12 }, 2048, device.m_UploadLimits) &&
			!IsConstantBufferBindingInRange({ first.m_Buffer, 2048, 4 }, 2048, device.m_UploadLimits),
			"Shared range checks reject overflow, misalignment, fractional elements and end-of-buffer slices");
		const auto leased = pool.GetStatistics();
		context.Check(leased.m_Total.m_CapacityInBytes == 2048 && leased.m_Leased.m_PageCount == 1 &&
			leased.m_Leased.m_CapacityInBytes == 2048 && leased.m_Idle.m_PageCount == 0,
			"Live page statistics measure leased capacity rather than payload bytes");
		const RHIFencePoint completion{ RHIFenceHandle{ 1, 1 }, 9 };
		context.Check(batch->Publish() && batch->Submitted(completion), "The mixed upload batch submits");
		graphics.EndRecordingScope();
		compute.EndRecordingScope();
		batch->Detach(writer);
		const auto pending = pool.GetStatistics();
		context.Check(pending.m_Pending.m_PageCount == 1 && pending.m_SubmittedBatchCount == 1 &&
			pending.m_LastBatch.m_UploadCount == 3 && pending.m_LastBatch.m_RequestedBytes == 40 &&
			pending.m_LastBatch.m_ReservedBytes == 1280 && pending.m_LastBatch.m_PageCount == 1 &&
			pending.m_HighWaterReservedBytesPerBatch == 1280 && device.IsUploadStorage(first.m_Buffer),
			"Completed batch statistics retain payload, reservation, capacity state and submission counts");
		context.Check(pending.m_LastBatch.m_LeasedBytes == 2048 &&
			pending.m_HighWaterLeasedBytesPerBatch == 2048 && pending.m_HighWaterPagesPerBatch == 1,
			"Batch statistics charge the full leased page capacity, not the reserved bytes");
		device.m_CompletedFenceValue = 9;
		pool.ReclaimCompleted();
		context.Check(pool.GetStatistics().m_Idle.m_CapacityInBytes == 2048 &&
			device.IsUploadStorage(first.m_Buffer), "Completed idle pages keep their upload-storage marker");

		for (uint32_t scenario = 0; scenario < 5; ++scenario)
		{
			auto invalid = pool.BeginBatch();
			invalid->Attach(writer);
			graphics.BeginRecordingScope(writer);
			compute.BeginRecordingScope(writer);
			const auto value = writer.UploadStructured(std::span(elements));
			auto rejected = value;
			bool accepted = false;
			if (scenario == 0)
			{
				rejected.m_UploadBatchId = 0;
				accepted = graphics.SetReadOnlyBuffer(3, rejected);
			}
			else if (scenario == 1)
			{
				accepted = graphics.SetConstantBuffer(0, { value.m_Buffer, value.m_OffsetInBytes, 4, 0 });
			}
			else if (scenario == 2)
			{
				accepted = graphics.SetReadOnlyBuffer(3, structured);
			}
			else if (scenario == 3)
			{
				rejected.m_SizeInBytes = 4092;
				accepted = graphics.SetReadOnlyBuffer(3, rejected);
			}
			else
			{
				accepted = frameUploads.UploadStructured(std::span(elements)).IsValid();
			}
			context.Check(!accepted && invalid->HasFailed() && graphics.HasRecordingError() &&
				compute.HasRecordingError() && !invalid->Publish(),
				"Stripped ids, stale structured bindings, forged ranges and stale capabilities poison the shared batch");
			graphics.EndRecordingScope();
			compute.EndRecordingScope();
			invalid->Detach(writer);
			invalid->Cancel();
		}
		auto quarantined = pool.BeginBatch();
		const auto held = quarantined->Upload(std::as_bytes(std::span(constants)));
		context.Check(held.IsValid() && quarantined->Publish(), "A mixed page can be quarantined");
		quarantined->RecordAcceptedSubmission();
		quarantined->SubmittedWithoutCompletionFence();
		const auto quarantine = pool.GetStatistics();
		context.Check(quarantine.m_Quarantined.m_PageCount == 1 &&
			quarantine.m_Quarantined.m_CapacityInBytes == 2048 && device.IsUploadStorage(held.m_Buffer),
			"Quarantined page capacity and its marker remain visible independently of fence completion");
		pool.ReleaseQuarantinedAfterVerifiedIdle();
		context.Check(pool.GetStatistics().m_Idle.m_PageCount == 1 &&
			pool.GetStatistics().m_SubmittedBatchCount == 2 && pool.GetStatistics().m_CancelledBatchCount == 5,
			"Verified idle changes page state without losing batch outcome statistics");
	}

	void RunUploadBudgetTests(SelfTestContext& context) noexcept
	{
		const std::array<uint32_t, 1> payload{ 7 };
		{
			// Two 768-byte uploads cannot share a 1024-byte page, so the rollover leaves
			// unused capacity that the batch budget still charges.
			RecordingDevice device;
			device.m_PassConstantsEnabled = true;
			RHIUploadPool pool(device, { .m_PageSizeInBytes = 1024 });
			const std::array<uint32_t, 192> slice{};
			auto heavy = pool.BeginBatch();
			const bool heavyUploaded = heavy->Upload(std::as_bytes(std::span(slice))).IsValid() &&
				heavy->Upload(std::as_bytes(std::span(slice))).IsValid();
			heavy->Cancel();
			auto light = pool.BeginBatch();
			const bool lightUploaded = light->Upload(std::as_bytes(std::span(payload))).IsValid();
			light->Cancel();
			const auto statistics = pool.GetStatistics();
			context.Check(heavyUploaded && lightUploaded &&
				statistics.m_LastBatch.m_LeasedBytes == 1024 &&
				statistics.m_LastBatch.m_PageCount == 1 &&
				statistics.m_HighWaterReservedBytesPerBatch == 1536 &&
				statistics.m_HighWaterLeasedBytesPerBatch == 2048 &&
				statistics.m_HighWaterPagesPerBatch == 2,
				"Per-batch high-water keeps leased capacity and pages across rollover");
		}
		{
			RecordingDevice device;
			device.m_PassConstantsEnabled = true;
			RHIUploadPool pool(device, { .m_PageSizeInBytes = 256, .m_MaxPoolBytes = 256 });
			auto held = pool.BeginBatch();
			context.Check(held->Upload(std::as_bytes(std::span(payload))).IsValid() && held->Publish(),
				"A quarantine budget probe publishes its payload");
			held->RecordAcceptedSubmission();
			held->SubmittedWithoutCompletionFence();
			auto next = pool.BeginBatch();
			const bool uploaded = next->Upload(std::as_bytes(std::span(payload))).IsValid();
			next->Cancel();
			const auto statistics = pool.GetStatistics();
			context.Check(!uploaded && statistics.m_Quarantined.m_CapacityInBytes == 256 &&
				statistics.m_PoolByteBudgetFailures == 1 && statistics.m_Total.m_PageCount == 1,
				"Quarantined pages consume pool byte budget and cannot be reused or excluded from accounting");
			pool.ReleaseQuarantinedAfterVerifiedIdle();
		}
		{
			RecordingDevice device;
			device.m_PassConstantsEnabled = true;
			RHIUploadPool pool(device, { .m_PageSizeInBytes = 256, .m_MaxBytesPerBatch = 256 });
			auto batch = pool.BeginBatch();
			const bool first = batch->Upload(std::as_bytes(std::span(payload))).IsValid();
			const bool second = batch->Upload(std::as_bytes(std::span(payload))).IsValid();
			batch->Cancel();
			const auto statistics = pool.GetStatistics();
			context.Check(first && !second && statistics.m_BatchByteBudgetFailures == 1 &&
				statistics.m_PageCountFailures == 0 && statistics.m_LastBatch.m_ReservedBytes == 256,
				"A batch byte budget fails before the independent page-count cap");
		}
		{
			RecordingDevice device;
			device.m_PassConstantsEnabled = true;
			RHIUploadPool pool(device, { .m_PageSizeInBytes = 256, .m_MaxUploadSizeInBytes = 1024,
				.m_MaxPoolBytes = 1024 });
			const std::array<uint32_t, 128> large{};
			const std::array<uint32_t, 192> larger{};
			auto batch = pool.BeginBatch();
			const auto dedicated = batch->UploadStructured(std::as_bytes(std::span(large)), 4);
			batch->Cancel();
			auto next = pool.BeginBatch();
			const bool overflow = next->UploadStructured(std::as_bytes(std::span(larger)), 4).IsValid();
			next->Cancel();
			const auto statistics = pool.GetStatistics();
			context.Check(dedicated.IsValid() && !overflow && statistics.m_Total.m_PageCount == 1 &&
				statistics.m_Idle.m_CapacityInBytes == 512 && statistics.m_PoolByteBudgetFailures == 1 &&
				statistics.m_PageCountFailures == 0,
				"Idle dedicated pages still consume pool byte budget when another page cannot reuse them");
			auto reuse = pool.BeginBatch();
			const auto reused = reuse->Upload(std::as_bytes(std::span(payload)));
			context.Check(reused.m_Buffer == dedicated.m_Buffer &&
				pool.GetStatistics().m_Leased.m_CapacityInBytes == 512,
				"A free large page serves a small upload while charging its full capacity");
			reuse->Cancel();
		}
		for (uint32_t scenario = 0; scenario < 6; ++scenario)
		{
			RecordingDevice device;
			device.m_PassConstantsEnabled = true;
			device.m_ConstantCreateFails = scenario == 0;
			device.m_ConstantMapFails = scenario == 1;
			device.m_ConstantPublishFails = scenario == 2;
			if (scenario == 3)
			{
				device.m_UploadLimits.m_MaxConstantRangeInBytes = 2;
			}
			if (scenario == 5)
			{
				device.m_UploadLimits.m_MaxStorageRangeInBytes = 2;
			}
			RHIUploadPool pool(device, { .m_MaxUploadSizeInBytes = scenario == 4 ? 2u : 64u * 1024u });
			auto batch = pool.BeginBatch();
			const bool uploaded = scenario == 5 ?
				batch->UploadStructured(std::as_bytes(std::span(payload)), 4).IsValid() :
				batch->Upload(std::as_bytes(std::span(payload))).IsValid();
			const bool published = batch->Publish();
			batch->Cancel();
			const auto statistics = pool.GetStatistics();
			context.Check(!published && batch->HasFailed() && statistics.m_CancelledBatchCount == 1 &&
				statistics.m_PageCreationFailures == (scenario == 0 ? 1u : 0u) &&
				statistics.m_PageMappingFailures == (scenario == 1 ? 1u : 0u) &&
				statistics.m_PublicationFailures == (scenario == 2 ? 1u : 0u) &&
				statistics.m_OversizedUploadFailures == (scenario >= 3 ? 1u : 0u) &&
				uploaded == (scenario == 2),
				"Upload failures retain their distinct cause counters and never publish a successful batch");
		}
	}

	void RunUploadTeardownContractTests(SelfTestContext& context) noexcept
	{
		const std::array<uint32_t, 4> payload{ 1, 2, 3, 4 };
		const auto quarantineOnePage = [&](RHIUploadPool& pool)
			{
				auto batch = pool.BeginBatch();
				const bool published =
					batch->Upload(std::as_bytes(std::span(payload))).IsValid() && batch->Publish();
				batch->RecordAcceptedSubmission();
				batch->SubmittedWithoutCompletionFence();
				return published && pool.GetQuarantinedPageCount() == 1;
			};

		// A quarantined page has no recorded use that could defer its destruction, so a
		// pool destroyed before verified idle or confirmed loss must leak it.
		{
			RecordingDevice device;
			device.m_PassConstantsEnabled = true;
			bool quarantined = false;
			{
				RHIUploadPool pool(device);
				quarantined = quarantineOnePage(pool);
			}
			context.Check(quarantined && device.m_AtmosphereDestroyedBufferCount == 0 &&
				device.m_PassConstantUnmapCount == 0,
				"Pool destruction leaks quarantined pages instead of freeing memory the GPU may read");
		}
		{
			RecordingDevice device;
			device.m_PassConstantsEnabled = true;
			bool released = false;
			{
				RHIUploadPool pool(device);
				released = quarantineOnePage(pool);
				pool.ReleaseQuarantinedAfterDeviceLoss();
				released = released && pool.GetQuarantinedPageCount() == 0;
			}
			context.Check(released && device.m_AtmosphereDestroyedBufferCount == 1 &&
				device.m_PassConstantUnmapCount == 1,
				"Confirmed device loss releases quarantined pages for destruction");
		}

#if defined(NDEBUG)
		// Debug asserts on the violated cancellation invariant. Release must quarantine the
		// pages instead of releasing leases that accepted work may still read.
		{
			RecordingDevice device;
			device.m_PassConstantsEnabled = true;
			RHIUploadPool pool(device);
			auto batch = pool.BeginBatch();
			const bool published =
				batch->Upload(std::as_bytes(std::span(payload))).IsValid() && batch->Publish();
			batch->RecordAcceptedSubmission();
			batch->Cancel();
			const RHIUploadStatistics statistics = pool.GetStatistics();
			context.Check(published && pool.GetQuarantinedPageCount() == 1 &&
				statistics.m_Leased.m_PageCount == 0 && statistics.m_CancelledBatchCount == 0,
				"Cancelling an accepted batch quarantines its pages in release builds");
			pool.ReleaseQuarantinedAfterVerifiedIdle();
		}
#endif
	}

	void RunUploadOwnerThreadContractTests(SelfTestContext& context) noexcept
	{
		RecordingDevice device;
		device.m_PassConstantsEnabled = true;
		RHIUploadPool pool(device);
		RHIUploadWriter writer;
		auto batch = pool.BeginBatch();
		batch->Attach(writer);
		const RHIFrameUploads uploads = writer.GetFrameUploads();
		const std::array<uint32_t, 4> payload{ 1, 2, 3, 4 };
		RHIConstantBufferBinding foreignConstants{};
		RHIReadOnlyBufferBinding foreignStructured{};
		RecordingGraphicsCommandContext graphics;
		graphics.BeginRecordingScope(writer);
		auto foreignBacking = std::make_shared<uint32_t>(73);
		std::weak_ptr<uint32_t> foreignWeak = foreignBacking;
		bool foreignRetained = true;
		std::thread foreign([&]()
			{
				foreignConstants = uploads.UploadConstants(payload);
				foreignStructured = uploads.UploadStructured(std::span<const uint32_t>(payload));
				foreignRetained = graphics.RetainForSubmission(std::move(foreignBacking));
			});
		foreign.join();
		// A foreign thread is rejected without touching batch state; only the encoder that
		// binds the invalid result fails the recording.
		context.Check(!foreignConstants.IsValid() && !foreignStructured.IsValid() &&
			!batch->HasFailed() && batch->GetPageCount() == 0 && pool.GetPageCount() == 0,
			"Off-owner uploads return invalid bindings without touching the batch");
		context.Check(!foreignRetained && foreignWeak.expired() &&
			!graphics.HasRecordingError() && !batch->HasFailed(),
			"Off-owner native retention rejects without mutating the batch or encoder");
		context.Check(graphics.RetainForSubmission(std::make_shared<uint32_t>(91)) &&
			!graphics.HasRecordingError(),
			"The owner encoder still retains native backing after a rejected foreign call");
		const RHIConstantBufferBinding owned = uploads.UploadConstants(payload);
		context.Check(owned.IsValid() && writer.Owns(owned) && !batch->HasFailed(),
			"The owner thread still uploads into a batch after a rejected foreign upload");
		graphics.EndRecordingScope();
		batch->Detach(writer);
		batch->Cancel();
	}

	void RunUploadWriterContractTests(SelfTestContext& context) noexcept
	{
		static_assert(!std::is_copy_constructible_v<RHIUploadWriter> &&
			!std::is_copy_assignable_v<RHIUploadWriter> &&
			!std::is_move_constructible_v<RHIUploadWriter> &&
			!std::is_move_assignable_v<RHIUploadWriter>);
		static_assert(std::is_copy_constructible_v<RHIFrameUploads> &&
			std::is_copy_assignable_v<RHIFrameUploads>);

		const auto passIndex = static_cast<uint32_t>(CommonRSRootParamIndex::PassCB);
		RecordingDevice device;
		device.m_PassConstantsEnabled = true;
		device.m_UseControlledFenceCompletion = true;
		device.m_CompletedFenceValue = 0;
		RHIUploadPool pool(device);
		RHIUploadWriter writer;
		RecordingGraphicsCommandContext graphics;
		RecordingComputeCommandContext compute;
		const std::array<uint32_t, 4> frameData{ 1, 2, 3, 4 };

		const RHIFrameUploads unattached = writer.GetFrameUploads();
		context.Check(writer.GetBatchId() == 0 && !writer.IsRecording() &&
			!unattached.IsCurrent() && !unattached.UploadConstants(frameData).IsValid(),
			"Frame uploads issued without an attached batch never upload");

		auto aborted = pool.BeginBatch();
		aborted->Attach(writer);
		const RHIFrameUploads abortedUploads = writer.GetFrameUploads();
		const RHIConstantBufferBinding abortedBinding = abortedUploads.UploadConstants(frameData);
		const bool abortPublished = aborted->Publish();
		aborted->Cancel();
		aborted->Detach(writer);
		context.Check(abortedBinding.IsValid() && abortPublished && !abortedUploads.IsCurrent() &&
			pool.GetPendingPageCount() == 0 && device.m_PassConstantUnmapCount == 0,
			"A first-frame published abort invalidates its capability without a fence or unmap");

		auto first = pool.BeginBatch();
		first->Attach(writer);
		const RHIFrameUploads frameUploads = writer.GetFrameUploads();
		const RHIConstantBufferBinding prepared = frameUploads.UploadConstants(frameData);
		graphics.BeginRecordingScope(writer);
		compute.BeginRecordingScope(writer);
		context.Check(frameUploads.IsCurrent() && frameUploads.GetBatchId() == first->GetId() &&
			prepared.IsValid() && prepared.m_Buffer == abortedBinding.m_Buffer &&
			prepared.m_UploadBatchId == first->GetId() &&
			writer.Owns(prepared) && graphics.SetConstantBuffer(passIndex, prepared) &&
			compute.SetConstantBuffer(passIndex, prepared) && !graphics.HasRecordingError() &&
			!compute.HasRecordingError(),
			"A frame-preparation upload binds on the graphics and direct-compute encoders of its batch");
		graphics.EndRecordingScope();
		compute.EndRecordingScope();
		const RHIFencePoint firstFence{ RHIFenceHandle{ 1, 1 }, 1 };
		const bool firstSubmitted = first->Publish() && first->Submitted(firstFence);
		first->Detach(writer);
		context.Check(firstSubmitted && !frameUploads.IsCurrent() &&
			!frameUploads.UploadConstants(frameData).IsValid(),
			"Frame uploads kept past their frame fail while the writer is detached");

		auto inFlight = pool.BeginBatch();
		inFlight->Attach(writer);
		const RHIConstantBufferBinding fresh = writer.GetFrameUploads().UploadConstants(frameData);
		const bool staleUploadRejected = !frameUploads.UploadConstants(frameData).IsValid();
		graphics.BeginRecordingScope(writer);
		context.Check(fresh.IsValid() && fresh.m_Buffer != prepared.m_Buffer &&
			staleUploadRejected && inFlight->HasFailed() && inFlight->GetPageCount() == 1,
			"Frame uploads from an earlier batch fail the later batch on the same writer");
		context.Check(!graphics.SetConstantBuffer(passIndex, prepared) && graphics.HasRecordingError(),
			"A binding kept from an earlier batch fails the recording of a later one");
		graphics.EndRecordingScope();
		inFlight->Detach(writer);
		inFlight->Cancel();

		pool.ReclaimCompleted();
		const bool heldUntilCompletion = pool.GetPendingPageCount() == 1;
		device.m_CompletedFenceValue = firstFence.m_Value;
		auto reuse = pool.BeginBatch();
		reuse->Attach(writer);
		const RHIConstantBufferBinding reused = writer.GetFrameUploads().UploadConstants(frameData);
		context.Check(heldUntilCompletion && reused.m_Buffer == prepared.m_Buffer &&
			reused.m_UploadBatchId != prepared.m_UploadBatchId && !writer.Owns(prepared),
			"A submitted page is reused only after its fence completes, under a new batch identity");
		reuse->Detach(writer);
		reuse->Cancel();
	}
}

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingUploadsSelfTests(SelfTestContext& context) noexcept
	{
		RunUploadWriterContractTests(context);
		RunUploadStorageContractTests(context);
		RunSubmissionRetentionContractTests(context);
		RunUploadBudgetTests(context);
		RunUploadTeardownContractTests(context);
		RunUploadOwnerThreadContractTests(context);
	}
}
