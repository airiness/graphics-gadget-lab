#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "GGLabFoundation/Platform/Win/Win32ProcessUtils.h"
#include "GGLabTestCore/SelfTest.h"
#include "GpuFaultLifecycleSelfTests.h"
#include "RuntimeIntegrationTestCases.h"

#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace gglab
{
	using namespace runtime_test;

	void RunGpuFaultLifecycleSelfTests(SelfTestContext& context) noexcept
	{
		const auto root = std::filesystem::temp_directory_path() /
			("gglab-fault-lifecycle-" + std::to_string(GetCurrentProcessId()) + "-" +
				std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		RunServicePublicationScenario(context, root / "service-publication");
		RunControlDeadlineScenario(context, root / "control-deadline");
		RunHostParameterScenario(context, root / "host-demo", false);
		RunHostParameterScenario(context, root / "host-lab", true);
		RunEffectiveParameterSettingsScenario(context, root / "tooling-host", false);
		RunEffectiveParameterSettingsScenario(context, root / "tooling-lab", true);
		RunHostViewControlScenario(context, root / "host-view-control");
		RunHostViewOverrideScenario(context, root / "host-view-overrides");
		RunToolingViewControlScenario(context, root / "tooling-view-control");
		RunToolingReleaseFailureScenario(context, root / "tooling-release-failure");
		RunParameterRequestScenario(context, root / "parameter-request");
		RunProtectedParameterScenario(context, root / "protected-parameters");
		RunLightingParameterScenario(context, root / "lighting-parameters");
		RunBatchPreparationScenario(context, root / "batch-preparation");
		RunContentBindingScenario(context, root / "content-binding-shutdown", false);
		RunContentBindingScenario(context, root / "content-binding-failure", true);
		RunLightingProfileRegistrationScenario(context, root / "profile-source-binding", true, false);
		RunLightingProfileRegistrationScenario(context, root / "profile-invalid-registration", false, false);
		RunLightingProfileRegistrationScenario(context, root / "profile-equivalent-registration", false, true);
		RunLightingProfileScenario(context, root / "lighting-profiles");
		RunLightingProfileScenario(context, root / "protected-lighting-profiles", true);
		RunLabWorldLightingScenario(context, root / "lab-world-lighting");
		RunSequenceParameterEventScenario(context, root / "sequence-events");
		RunDeferredSequenceEventScenario(context, root / "deferred-sequence-events");
		RunSessionAdmissionScenario(context, root / "session-admission");
		RunSessionExitAdmissionScenario(context, root / "session-exit-admission");
		RunParameterFeedbackScenario(context, root / "parameter-feedback");
		RunPendingParameterBatchScenario(context, root / "pending-parameter-batches");
		RunInheritedLightingScenario(context, root / "inherited-lighting");
		RunLabSessionRetirementScenario(context, root / "lab-session-retirement");
		RunParameterFrameScenario(context, root / "parameter-frames", false);
		RunRebuildParameterScenario(context, root / "parameter-rebuild");
		RunRebuildParameterScenario(context, root / "protected-rebuild", true);
		RunParameterFrameScenario(context, root / "parameter-failed-submit", true);
		RunFrameOutcomeScenarios(context, root / "outcomes");
		RunOwnedJobRoutingScenario(context, root / "owned-job-routing");
		RunOwnedJobFailureScenario(context, root / "owned-job-failed-fenced", FrameEndInjection::FatalFenced, false);
		RunOwnedJobFailureScenario(context, root / "owned-job-failed-unfenced", FrameEndInjection::FatalUnfenced, false);
		RunOwnedJobFailureScenario(context, root / "owned-job-unknown", FrameEndInjection::Completed, true);
		RunOwnedWriterDrainScenario(context, root / "owned-writer-drain");
		RunAdmittedJobScenario(context, root / "admitted-job", false);
		RunAdmittedJobScenario(context, root / "admitted-job-missing", true);
		RunAdmittedJobScenario(context, root / "admitted-job-multiple-images", false, true);
		RunJobCancellationScenario(context, root / "cancel-before-submit", true);
		RunJobCancellationScenario(context, root / "cancel-shared-submission", false);
		RunRequiredPlanOutcomeScenario(context, root / "required-plan-late", 0, false);
		RunRequiredPlanOutcomeScenario(context, root / "required-plan-reference", 3, false);
		RunRequiredPlanOutcomeScenario(context, root / "required-plan-missing", 0, true);
		RunPublicationRetentionScenario(context, root / "publication-before-claim", false);
		RunPublicationRetentionScenario(context, root / "publication-after-claim", true);
		RunGpuTeardownScenarios(context, root);
		std::error_code error;
		std::filesystem::remove_all(root, error);
	}
}
