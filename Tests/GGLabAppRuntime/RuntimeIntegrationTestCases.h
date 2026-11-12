#pragma once

#include "RuntimeIntegrationTestServices.h"

namespace gglab::runtime_test
{
	RHIFrameEndResult RunOutcomeHostFrame(SelfTestContext& checks, GGLabAppRuntime& runtime,
		LifetimeProbe& probe, HostFrameAction action);
	void RunEndFrameOutcomeScenario(SelfTestContext& checks, const std::filesystem::path& root,
		FrameEndInjection injection, AbortInjection abort);
	void RunDiscardOutcomeScenario(SelfTestContext& checks, const std::filesystem::path& root,
		HostFrameAction action, AbortInjection abort);
	void RunExportDeferralOutcomeScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunRequiredPlanOutcomeScenario(SelfTestContext& checks, const std::filesystem::path& root,
		uint32_t referenceSamples, bool missingResource);
	void RunOwnedJobRoutingScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunOwnedJobFailureScenario(SelfTestContext& checks, const std::filesystem::path& root,
		FrameEndInjection injection, bool completionUnknown);
	void RunOwnedWriterDrainScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunAdmittedJobScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool missingResource, bool multipleImages = false);
	void RunJobCancellationScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool beforeSubmit);
	void RunPreviewChangeScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunLateCommitScenario(SelfTestContext& checks, const std::filesystem::path& root,
		CommitEnqueueFault fault, AbortInjection abort, bool throughRuntime = true);
	void RunForwardPlusValidationScenario(SelfTestContext& checks, const std::filesystem::path& root,
		RHIFenceQueryResult completion, bool resize, bool nativeQuery);
	void RunPublicationRetentionScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool claimed);
	void RunHostParameterScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool withLab);
	void RunEffectiveParameterSettingsScenario(SelfTestContext& checks,
		const std::filesystem::path& root, bool withLab);
	void RunHostViewControlScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunToolingViewControlScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunToolingReleaseFailureScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunHostViewOverrideScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunProtectedParameterScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunParameterRequestScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunSequenceParameterEventScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunDeferredSequenceEventScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunSessionAdmissionScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunSessionExitAdmissionScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunParameterFeedbackScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunPendingParameterBatchScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunInheritedLightingScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunLabSessionRetirementScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunRebuildParameterScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool protectedAssignments = false);
	void RunParameterFrameScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool fatalSubmission);
	void RunLightingParameterScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunContentBindingScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool failBeforeShutdown);
	void RunBatchPreparationScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunLightingProfileRegistrationScenario(SelfTestContext& checks,
		const std::filesystem::path& root, bool labBoolean, bool equivalent);
	void RunLabWorldLightingScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunLightingProfileScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool protectedAssignments = false);
	void RunServicePublicationScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunControlDeadlineScenario(SelfTestContext& checks, const std::filesystem::path& root);
	void RunFrameOutcomeScenarios(SelfTestContext& checks, const std::filesystem::path& root);
	void RunScenario(SelfTestContext& checks, LifetimeProbe& probe, const std::filesystem::path& root);
	void RunGpuTeardownScenarios(SelfTestContext& context,
		const std::filesystem::path& root) noexcept;
}
