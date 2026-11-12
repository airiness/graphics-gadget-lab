#include "CaptureCoordinatorTestCases.h"
#include "FrameCaptureCoordinatorSelfTests.h"
#include "GGLabTestCore/SelfTest.h"

namespace gglab
{
	using namespace capture_test;

	void RunFrameCaptureCoordinatorSelfTests(SelfTestContext& context) noexcept
	{
		RunJobAdmissionTests(context);
		RunConcurrentJobReceiptTests(context);
		RunJobResultTests(context);
		RunJobCancellationTests(context);
		RunJobCoverageCancellationTests(context);
		RunJobWriterCancellationTests(context);
		RunJobEncoderCancellationTests(context);
		RunJobPublicationCancellationTests(context);
		RunBundleProvenanceTests(context);
		RunCaptureEnvironmentTests(context);
		RunBundlePublicationTests(context);
		RunBundleSingleProductTests(context);
		RunBundleManifestShutdownTests(context);
		RunJobShutdownTests(context);
		RunResourceWritingTests(context);
		RunResourceSequenceTests(context);
		RunSequenceGroupAdmissionTests(context);
		RunRequiredPlanLifecycleTests(context);
		RunRequiredPlanRetryTests(context);
		RunRequiredPlanWatchTests(context);
		RunSequenceLateDeferralTests(context);
		RunGraphWritingTests(context);
		RunGraphFailureTests(context);
		RunNextFrameTests(context);
		RunRequiredFrameStateTests(context);
		RunFrameSelectionDeadlineTests(context);
		RunAfterReadyTests(context);
		RunParameterSettlingTests(context);
		RunHistoryContinuitySettlingTests(context);
		RunLoadingHistoryTests(context);
		RunReferenceViewTests(context);
		RunFailureTests(context);
		RunShutdownTests(context);
		RunWatchShutdownTests(context);
		RunNameCollisionTests(context);
		RunWriterThreadTests(context);
		RunCapacityAdmissionTests(context);
		RunDeferredCapacityTests(context);
		RunWriterQueueLimitTests(context);
		RunAbandonedWritingTests(context);
		RunPublicationFaultTests(context);
		RunRenamePublicationTests(context);
		RunPublicationShutdownTests(context);
		RunMetadataSerializationTests(context);
		RunGIProbeInspectionTests(context);
		RunGIProbePickingTests(context);
		RunGIReceiverInspectionTests(context);
		RunGIInspectionCancellationTests(context);
		RunFrameSequenceTests(context);
		RunGIFieldLightingRefreshTests(context);
		RunFrameSequenceBackPressureTests(context);
		RunSequenceJobOwnershipTests(context);
		RunDiagnosticCaptureTests(context);
		RunDiagnosticSchedulingTests(context);
		RunFrameResultIdentityTests(context);
		RunAttemptRetryTests(context);
		RunReferenceSequenceTests(context);
		RunFrameSequenceEvaluationTests(context);
		RunFrameSequenceGpuTimingTests(context);
		RunSequenceTimingGraphObservationTests(context);
		RunSequenceTimingPublicationTests(context);
		RunSequenceParameterPublicationTests(context);
		RunFrameSequenceContinuityTests(context);
		RunFrameSequenceFailureTests(context);
	}
}
