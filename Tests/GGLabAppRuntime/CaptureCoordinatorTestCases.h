#pragma once

#include "GGLabTestCore/SelfTest.h"

namespace gglab::capture_test
{
	void RunNextFrameTests(SelfTestContext& context) noexcept;
	void RunFrameSelectionDeadlineTests(SelfTestContext& context) noexcept;
	void RunRequiredFrameStateTests(SelfTestContext& context) noexcept;
	void RunAfterReadyTests(SelfTestContext& context) noexcept;
	void RunParameterSettlingTests(SelfTestContext& context) noexcept;
	void RunHistoryContinuitySettlingTests(SelfTestContext& context) noexcept;
	// Temporal history keeps accumulating while content loads. An after-ready
	// capture that settled over such history would depend on how many frames
	// loading took, so settling and time start at a camera cut on a ready frame.
	void RunLoadingHistoryTests(SelfTestContext& context) noexcept;
	void RunReferenceViewTests(SelfTestContext& context) noexcept;
	void RunFailureTests(SelfTestContext& context) noexcept;
	void RunShutdownTests(SelfTestContext& context) noexcept;
	void RunWatchShutdownTests(SelfTestContext& context) noexcept;
	void RunNameCollisionTests(SelfTestContext& context) noexcept;
	void RunWriterThreadTests(SelfTestContext& context) noexcept;
	void RunCapacityAdmissionTests(SelfTestContext& context) noexcept;
	void RunDeferredCapacityTests(SelfTestContext& context) noexcept;
	void RunWriterQueueLimitTests(SelfTestContext& context) noexcept;
	void RunAbandonedWritingTests(SelfTestContext& context) noexcept;
	void RunPublicationFaultTests(SelfTestContext& context) noexcept;
	void RunRenamePublicationTests(SelfTestContext& context) noexcept;
	void RunPublicationShutdownTests(SelfTestContext& context) noexcept;
	void RunGIProbePickingTests(SelfTestContext& context) noexcept;
	void RunGIProbeInspectionTests(SelfTestContext& context) noexcept;
	void RunGIReceiverInspectionTests(SelfTestContext& context) noexcept;
	void RunGIInspectionCancellationTests(SelfTestContext& context) noexcept;
	void RunMetadataSerializationTests(SelfTestContext& context) noexcept;
	void RunFrameSequenceTests(SelfTestContext& context) noexcept;
	void RunGIFieldLightingRefreshTests(SelfTestContext& context) noexcept;
	void RunFrameSequenceBackPressureTests(SelfTestContext& context) noexcept;
	void RunSequenceJobOwnershipTests(SelfTestContext& context) noexcept;
	void RunDiagnosticCaptureTests(SelfTestContext& context) noexcept;
	void RunDiagnosticSchedulingTests(SelfTestContext& context) noexcept;
	void RunAttemptRetryTests(SelfTestContext& context) noexcept;
	void RunFrameResultIdentityTests(SelfTestContext& context) noexcept;
	void RunReferenceSequenceTests(SelfTestContext& context) noexcept;
	void RunFrameSequenceEvaluationTests(SelfTestContext& context) noexcept;
	void RunSequenceTimingGraphObservationTests(SelfTestContext& context) noexcept;
	void RunSequenceParameterPublicationTests(SelfTestContext& context) noexcept;
	void RunSequenceTimingPublicationTests(SelfTestContext& context) noexcept;
	void RunFrameSequenceGpuTimingTests(SelfTestContext& context) noexcept;
	void RunFrameSequenceContinuityTests(SelfTestContext& context) noexcept;
	void RunResourceWritingTests(SelfTestContext& context) noexcept;
	void RunJobAdmissionTests(SelfTestContext& context) noexcept;
	void RunConcurrentJobReceiptTests(SelfTestContext& context) noexcept;
	void RunJobResultTests(SelfTestContext& context) noexcept;
	void RunJobCancellationTests(SelfTestContext& context) noexcept;
	void RunJobCoverageCancellationTests(SelfTestContext& context) noexcept;
	void RunJobWriterCancellationTests(SelfTestContext& context) noexcept;
	void RunJobEncoderCancellationTests(SelfTestContext& context) noexcept;
	void RunJobPublicationCancellationTests(SelfTestContext& context) noexcept;
	void RunBundleProvenanceTests(SelfTestContext& context) noexcept;
	void RunCaptureEnvironmentTests(SelfTestContext& context) noexcept;
	void RunBundlePublicationTests(SelfTestContext& context) noexcept;
	void RunBundleSingleProductTests(SelfTestContext& context) noexcept;
	void RunBundleManifestShutdownTests(SelfTestContext& context) noexcept;
	void RunJobShutdownTests(SelfTestContext& context) noexcept;
	void RunRequiredPlanLifecycleTests(SelfTestContext& context) noexcept;
	void RunRequiredPlanRetryTests(SelfTestContext& context) noexcept;
	void RunRequiredPlanWatchTests(SelfTestContext& context) noexcept;
	void RunSequenceLateDeferralTests(SelfTestContext& context) noexcept;
	void RunSequenceGroupAdmissionTests(SelfTestContext& context) noexcept;
	void RunResourceSequenceTests(SelfTestContext& context) noexcept;
	void RunGraphWritingTests(SelfTestContext& context) noexcept;
	void RunGraphFailureTests(SelfTestContext& context) noexcept;
	void RunFrameSequenceFailureTests(SelfTestContext& context) noexcept;
}
