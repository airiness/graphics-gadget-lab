#pragma once

#include "ApplicationContentRegistration.h"
#include "ApplicationInput.h"
#include "ApplicationToolingIntegration.h"
#include "Capture/CaptureJobTypes.h"
#include "Capture/FrameCaptureImageEncoder.h"
#include "Demo/DemoTypes.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlusDebugReadback.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RenderServicesView.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabTestCore/SelfTest.h"
#include "Lab/LabParameter.h"
#include "Lab/LabRuntime.h"
#include "Lab/LabSessionBase.h"
#include "Parameters/ParameterRequest.h"
#include "Parameters/SessionParameterControl.h"
#include "Parameters/ViewControlSnapshot.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistryArtifact.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gglab
{
	struct AppRuntimeServicePublicationTestAccess
	{
		static bool RollBackPartialInitialization(GGLabAppRuntime& runtime) noexcept
		{
			runtime.m_ServicesInitialized = false;
			const bool unpublished = !runtime.GetRenderServices().IsValid();
			GGLAB_UNUSED(runtime.FailServiceInitialization(
				AppRuntimeServiceInitializeResult::StartupContentUnavailable));
			return unpublished;
		}
	};
}

namespace gglab::runtime_test
{
	enum class TeardownOutcome
	{
		Completed,
		DeviceLost,
		Timeout,
		Failed,
	};

	enum class FrameEndInjection
	{
		Completed,
		FatalFenced,
		FatalUnfenced,
		RecordingFailure,
	};

	enum class AbortInjection
	{
		Unfenced,
		Fenced,
		CompletionUnknown,
	};

	enum class CommitEnqueueFault
	{
		None,
		// After Render sealed the list, before the RHI submission.
		BeforeSubmission,
		// After the RHI accepted the submission, before the commits start.
		AfterSubmission,
		// While the commits of a Submitted frame run.
		DuringSettlement,
	};

	struct CommitObservation
	{
		uint64_t m_Serial = 0;
		RHIFencePoint m_Fence{};
		uint32_t m_Order = 0;
	};

	[[nodiscard]] inline ParameterRequest AssignmentRequest(GGLabAppRuntime& runtime,
		std::span<const LabParameterValue> values)
	{
		std::string error;
		if (!runtime.CanRequestParameters(error))
		{
			return {};
		}
		runtime.EnableParameterControl();
		return runtime.GetParameterControl()->MakeAssignmentRequest(values);
	}

	[[nodiscard]] inline ParameterRequest AssignmentRequest(SessionParameterControl& source,
		std::span<const LabParameterValue> values)
	{
		if (!source.IsEnabled())
		{
			source.Enable();
		}
		return source.MakeAssignmentRequest(values);
	}

	// Only the backend is simulated. Renderer, graph, capture, IBL, upload scheduler,
	// AppRuntime::Tick/Shutdown and their owners are the production implementations.
	struct LifetimeProbe
	{
		TeardownOutcome m_Outcome = TeardownOutcome::Completed;
		std::filesystem::path m_Receipt;
		bool m_EnableParameterLab = false;
		bool m_ReturnStaleLightingEvidence = false;
		bool m_ReturnMissingLightingEvidence = false;
		bool m_RequestLightingTwice = false;
		uint32_t m_LightingParameterApplications = 0;
		bool m_HoldLightingPreparation = false;
		// Contained fault-harness bindings; external retained capabilities use the view below.
		DemoServices m_ContentServices{};
		RenderServicesView m_SavedServiceView;
		bool m_CheckServiceCleanup = false;
		bool m_ContainedCleanupAfterRevocation = false;
		bool m_FrameBeginUnavailable = false;
		uint32_t m_ContentSerialOffset = 0;
		uint64_t m_PlannedContentIdentity = 0;
		uint64_t m_ExpectedReleasedBatch = 0;
		bool m_BindingReleasedBeforeExit = false;
		uint32_t m_DemoDestructions = 0;
		uint32_t m_LabSessionDestructions = 0;
		bool m_HoldRetirementPreparation = false;
		bool m_EnableLightingProfiles = false;
		bool m_ProfileNamedLabBool = false;
		bool m_InvalidLightingProfiles = false;
		bool m_EquivalentLightingProfiles = false;
		uint32_t m_LightingProfileQueries = 0;
		bool m_ObserveParameterCommit = false;
		bool m_ParameterCommitWasComplete = false;
		uint64_t m_ExpectedCommitGeneration = 0;
		LabRuntime* m_ParameterRuntime = nullptr;
		const SessionParameterControl* m_ParameterControl = nullptr;
		uint32_t m_ParameterApplications = 0;
		uint32_t m_ParameterSessionCreations = 0;
		bool m_HoldParameterPreparation = false;
		bool m_FailParameterPreparation = false;
		bool m_FailParameterCreation = false;
		std::function<void()> m_ParameterEffectCallback;
		std::function<void()> m_ParameterPrepareCallback;
		uint64_t m_ParameterBuiltSerial = 0;
		float m_ParameterBuiltRadius = 0.0f;
		ResolvedViewRenderSettings m_ParameterBuiltView{};
		ResolvedViewRenderSettings m_ParameterValidationView{};
		bool m_SkipParameterFrame = false;
		bool m_HoldLightingStages = false;
		std::string m_ExtraPendingGate;
		std::function<void()> m_ParameterBuiltCallback;
		bool m_SkipLightingPublication = false;
		bool m_RebakeAfterParameterBuild = false;
		bool m_UnrelatedLightingChange = false;
		EnvironmentLightingControlBase* m_ParameterEnvironment = nullptr;
		bool m_ChangeParameterAfterBuild = false;
		bool m_Inject = false;
		bool m_Faulted = false;
		bool m_SafeToRelease = true;
		bool m_HoldUploads = false;
		uint32_t m_EarlyDestructions = 0;
		uint32_t m_ReadbackMaps = 0;
		uint32_t m_LiveReadbackBuffers = 0;
		uint32_t m_Readbacks = 0;
		uint32_t m_CaptureCopies = 0;
		uint32_t m_LateSubmits = 0;
		uint32_t m_FenceWaitsAfterFault = 0;
		uint32_t m_FrameSubmits = 0;
		uint32_t m_IdleCalls = 0;
		uint32_t m_ContextDestructions = 0;
		uint32_t m_ReadyExecutions = 0;
		uint32_t m_UploadCallbacks = 0;
		bool m_UploadFailedSafely = false;
		std::atomic<uint32_t> m_ImageEncodes = 0;
		uint32_t m_QuarantinedPages = 0;
		RHIFencePoint m_ReadbackFence{};
		FrameEndInjection m_FrameEndInjection = FrameEndInjection::Completed;
		AbortInjection m_AbortInjection = AbortInjection::Unfenced;
		bool m_RejectUploadWait = false;
		bool m_QueueExport = false;
		uint32_t m_RequiredPlanDeferrals = 0;
		GGLabAppRuntime* m_AppRuntime = nullptr;
		uint64_t m_CancelJobDuringBuild = 0;
		std::optional<CaptureCancelResult> m_CancellationResult;
		bool m_HoldCaptureFence = false;
		bool m_EnableTemporalHistory = false;
		uint32_t m_FrameBegins = 0;
		uint32_t m_FrameEnds = 0;
		uint32_t m_FrameAborts = 0;
		uint32_t m_GraphBuilds = 0;
		uint32_t m_GraphExecutions = 0;
		uint32_t m_SubmittedCallbacks = 0;
		uint32_t m_RejectedUploadWaits = 0;
		RHIFencePoint m_LastEndFence{};
		RHIFencePoint m_LastAbortFence{};
		FrameCaptureControlBase* m_CaptureControl = nullptr;
		uint64_t m_ExportRequest = 0;
		bool m_EnableDeferredCommits = false;
		bool m_EnablePreviewCommits = false;
		bool m_ChangePreviewAfterBuild = false;
		CommitEnqueueFault m_CommitEnqueueFault = CommitEnqueueFault::None;
		std::optional<RGExecuteContext> m_RetainedExecuteContext;
		std::array<CommitObservation, 64> m_CommitObservations{};
		uint32_t m_CommitsRun = 0;
		uint32_t m_CommitPayloadsDestroyed = 0;
		uint32_t m_LateCommitsRun = 0;
		bool m_CommitIdentityMatches = true;
		RenderPresentationAccess* m_Presentation = nullptr;
		bool m_EnableForwardPlusReadback = false;
		bool m_ResizeValidation = false;
		bool m_ObserveForwardPlusQuery = true;
		std::optional<RHIFenceQueryResult> m_ForwardPlusQuery;
		RHIFencePoint m_ForwardPlusFence;
		ForwardPlusDebugReadback* m_ForwardPlusReadback = nullptr;
		RenderFrameValidationResult m_LastValidation;
		uint32_t m_ValidationPreparations = 0;

		void Record(std::string_view event) const
		{
			if (!m_Receipt.empty())
			{
				std::ofstream stream(m_Receipt, std::ios::app);
				stream << event << '\n';
			}
		}
		void Destroy()
		{
			if (m_Faulted && !m_SafeToRelease)
			{
				++m_EarlyDestructions;
				Record("early_destroy");
			}
		}
	};

	class FaultFactory final : public RHIContextFactoryBase
	{
	public:
		explicit FaultFactory(LifetimeProbe& probe) : m_Probe(probe) {}
		std::unique_ptr<RHIContext> CreateContext(const RHIContextDesc&) const noexcept override;
	private:
		LifetimeProbe& m_Probe;
	};

	enum class HostFrameAction
	{
		Submit,
		DiscardAfterBuild,
		DiscardAfterRecording,
		RejectUploadOrder,
	};

	std::unique_ptr<DemoBase> CreateFaultDemo(const DemoCreateInfo& info,
		const LabStartupSelection&, std::span<const LabRegistration>) noexcept;

	ShaderProgramRegistryArtifactRef WriteShaderFixtures(const std::filesystem::path& root);

	bool InitializeOutcomeRuntime(SelfTestContext& checks, GGLabAppRuntime& runtime,
		FaultFactory& factory, ApplicationInput& input, LifetimeProbe& probe,
		const std::filesystem::path& root, FrameCaptureImageEncoder imageEncoder = {},
		CaptureJobLimits jobLimits = {});

	std::unique_ptr<LabSessionBase> CreateInheritedLightingTestLab(const LabSessionCreateInfo& info) noexcept;

	std::unique_ptr<LabSessionBase> CreateRetirementTestLab(const LabSessionCreateInfo& info) noexcept;

	class ParameterTestTooling final : public ApplicationToolingIntegrationBase
	{
	public:
		std::function<void()> m_OnShutdown;
		ApplicationToolingFrameSettings m_Settings;
		bool m_Open = false;
		std::optional<ViewControlSnapshot> m_ViewControl;
		std::function<void(const ApplicationToolingFrameContext&)> m_OnDraw;
		mutable uint32_t m_Captures = 0;
		void PrepareForShutdown() noexcept override
		{
			if (m_OnShutdown)
			{
				m_OnShutdown();
			}
		}
		ApplicationToolingInputCapture GetPreviousFrameInputCapture() const noexcept override { return {}; }
		ApplicationToolingFrameSettings CaptureFrameSettings() const noexcept override
		{
			++m_Captures;
			return m_Settings;
		}
		bool BeginFrame() noexcept override { return m_Open; }
		void Draw(const ApplicationToolingFrameContext& context) noexcept override
		{
			m_ViewControl = context.m_ViewControl ? std::optional(*context.m_ViewControl) : std::nullopt;
			if (m_OnDraw)
			{
				m_OnDraw(context);
			}
		}
		void EndFrame(ApplicationToolingFrameEndReason) noexcept override {}
		RenderPipelineOverlayExtensionBase* GetOverlayExtension() noexcept override { return nullptr; }
	};

	class RuntimeTestDirectory final
	{
	public:
		explicit RuntimeTestDirectory(std::string_view name);
		~RuntimeTestDirectory();
		const std::filesystem::path& GetPath() const noexcept { return m_Path; }
	private:
		std::filesystem::path m_Path;
	};
}
