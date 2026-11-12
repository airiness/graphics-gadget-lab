#include "ApplicationInput.h"
#include "Demo/DemoManager.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RenderSceneTypes.h"
#include "GGLabTestCore/SelfTest.h"
#include "GpuFaultLifecycleSelfTests.h"
#include "Lab/LabParameter.h"
#include "Lab/LabRuntime.h"
#include "Lab/LabTypes.h"
#include "Parameters/ParameterBatch.h"
#include "Parameters/ParameterEffectReceipt.h"
#include "Parameters/ParameterFrameState.h"
#include "Parameters/ParameterRequest.h"
#include "RuntimeIntegrationTestCases.h"
#include "RuntimeIntegrationTestServices.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

namespace gglab::runtime_test
{
	void RunLabSessionRetirementScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		LabRuntime lab({ .m_Services = probe.m_ContentServices, .m_WindowWidth = 32,
			.m_WindowHeight = 32, .m_RunConfig = { .m_WarmupFrames = 0 } });
		const LabId labId("gglab.lab.test.retirement");
		checks.Check(lab.RegisterLab({ .m_Id = labId, .m_DisplayName = "Retirement fixture" },
			&CreateRetirementTestLab) && lab.Initialize(labId),
			"The retirement fixture initializes through the production LabRuntime");
		lab.TickTransitions();
		lab.OnEnter();

		lab.RequestRestartSession();
		lab.ProcessPendingCommands();
		checks.Check(lab.GetRetiringSessionCount() == 1 && probe.m_LabSessionDestructions == 0,
			"Activation keeps the replaced session alive in the retiring list");
		lab.TickTransitions();
		checks.Check(lab.GetRetiringSessionCount() == 0 && probe.m_LabSessionDestructions == 1,
			"A session that never reached a Completed frame is released at the next poll");

		const RHIFencePoint renderedFence{ RHIFenceHandle{ 1, 1 }, 1000 };
		probe.m_ReadbackFence = renderedFence;
		lab.OnFrameSubmitted({ .m_RenderSceneStatus = RenderSceneBuildStatus::Ready,
			.m_SubmittedFence = renderedFence });
		lab.RequestRestartSession();
		lab.ProcessPendingCommands();
		lab.TickTransitions();
		checks.Check(lab.GetRetiringSessionCount() == 1 && probe.m_LabSessionDestructions == 1,
			"A replaced session stays alive while its last Completed frame's fence is pending");
		probe.m_ReadbackFence = {};
		lab.TickTransitions();
		checks.Check(lab.GetRetiringSessionCount() == 0 && probe.m_LabSessionDestructions == 2,
			"The replaced session is destroyed once that fence completes");

		probe.m_HoldRetirementPreparation = true;
		lab.RequestRestartSession();
		lab.ProcessPendingCommands();
		lab.RequestRestartSession();
		lab.ProcessPendingCommands();
		checks.Check(lab.HasPendingSession() && lab.GetRetiringSessionCount() == 0 &&
			probe.m_LabSessionDestructions == 3,
			"A superseded pending session is destroyed at once and never retires");
		lab.Shutdown();
		checks.Check(probe.m_LabSessionDestructions == 5,
			"Shutdown destroys the pending and active sessions");
		runtime.Shutdown();
	}

	void RunRebuildParameterScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool protectedAssignments)
	{
		LifetimeProbe probe;
		probe.m_EnableParameterLab = true;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		auto& lab = *probe.m_ParameterRuntime;
		auto& control = *runtime.GetParameterControl();
		probe.m_ParameterControl = &control;
		runtime.EnableParameterControl();
		std::string error;
		const auto request = [&](std::string_view name, uint32_t value)
			{
				const std::array values{ LabParameterValue{ LabParameterId(name), value } };
				auto intent = AssignmentRequest(runtime, values);
				if (protectedAssignments)
				{
					const auto stamp = runtime.GetDomainControlStamp(ParameterControlDomain::Lab);
					if (!stamp)
					{
						error = "Protected Lab control is unavailable.";
						return uint64_t(0);
					}
					intent.m_ExpectedDomain = *stamp;
				}
				return runtime.RequestParameters(intent, error);
			};
		if (protectedAssignments)
		{
			const std::array baseline{ LabParameterValue{ LabParameterId("view.exposure.ev100"), 2.0f } };
			const auto configured = runtime.RequestParameters(AssignmentRequest(runtime, baseline), error);
			GGLAB_UNUSED(runtime.Tick());
			checks.Check(runtime.TakeParameterBatchResult(configured).has_value(),
				"Protected Rebuild starts with a separately owned Host override");
		}
		const auto created = probe.m_ParameterSessionCreations;
		const auto originalIdentity = control.GetContentIdentity();
		const auto revision = control.GetParameterRevision();
		for (const auto& invalid : std::array{
			LabParameterValue{ LabParameterId("unknown"), true },
			LabParameterValue{ LabParameterId("radius"), 5.0f },
			LabParameterValue{ LabParameterId("lighting"), uint32_t(2) } })
		{
			const std::array values{ LabParameterValue{ LabParameterId("layout"), uint32_t(2) },
				LabParameterValue{ LabParameterId("view.exposure.ev100"), 2.0f }, invalid };
			checks.Check(runtime.RequestParameters(AssignmentRequest(runtime, values), error) == 0 &&
				control.GetParameterRevision() == revision && probe.m_ParameterSessionCreations == created,
				"Invalid or old-World Deferred mixed rebuild batches reject before any source mutation");
		}
		const auto same = request("layout", 1);
		GGLAB_UNUSED(runtime.Tick());
		const auto unchanged = runtime.TakeParameterBatchResult(same);
		checks.Check(unchanged && unchanged->m_Items[0].m_State == ParameterEffectState::NoChange &&
			!unchanged->m_Items[0].m_Published && !unchanged->m_Items[0].m_FirstConsumed &&
			probe.m_ParameterSessionCreations == created && control.GetContentIdentity() == originalIdentity,
			"Re-setting a Rebuild value starts no transition and preserves content identity");
		probe.m_HoldParameterPreparation = true;
		const std::array mixed{
			LabParameterValue{ LabParameterId("layout"), uint32_t(2) },
			LabParameterValue{ LabParameterId("scene-layout"), uint32_t(2) },
			LabParameterValue{ LabParameterId("pipeline-layout"), uint32_t(2) },
			LabParameterValue{ LabParameterId("radius"), 2.0f },
			LabParameterValue{ LabParameterId("view.exposure.ev100"), 2.0f },
			};
		auto rebuildIntent = AssignmentRequest(runtime, mixed);
		if (protectedAssignments)
		{
			std::erase_if(rebuildIntent.m_Entries, [](const auto& entry)
				{
					return entry.m_Id == LabParameterId("view.exposure.ev100");
				});
			rebuildIntent.m_ExpectedDomain = runtime.GetDomainControlStamp(ParameterControlDomain::Lab);
		}
		const auto batchId = runtime.RequestParameters(rebuildIntent, error);
		checks.Check(batchId != 0 && lab.GetActiveSession()->GetParameters().Get(
			LabParameterId("layout"), uint32_t(0)) == 1, "Rebuild admission reserves without mutating the active Lab");
		{
			auto prepared = lab.PrepareParameterBatch(batchId, std::span(mixed).first(4), error);
			checks.Check(prepared && prepared->HasChanges() && prepared->GetRebuildGeneration() == 0 &&
				!lab.GetParameterRebuild() && probe.m_ParameterSessionCreations == created,
				"Preparing and dropping a Rebuild plan creates no operation, candidate or source mutation");
		}
		bool completeCommit = false;
		probe.m_ParameterEffectCallback = [&]()
			{
				completeCommit = control.GetParameterBatchStatus()->m_State == ParameterBatchState::Applying &&
					control.GetLabParameters()->Get(LabParameterId("layout"), uint32_t(0)) == 2 &&
					control.GetHostParameters().Get(LabParameterId("view.exposure.ev100"), 0.0f) == 2.0f;
			};
		GGLAB_UNUSED(runtime.Tick());
		const auto* preparing = control.GetParameterBatchStatus();
		checks.Check(completeCommit && preparing && !preparing->IsTerminal() && lab.HasPendingSession() &&
			probe.m_ParameterSessionCreations == created + 1 &&
			lab.GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f) == 1.0f &&
			control.GetLabParameters()->Get(LabParameterId("radius"), 0.0f) == 2.0f,
			"One candidate owns all requested Lab values while the old active World retains its inputs");
		const auto generation = preparing->m_Items[0].m_Identity;
		const auto batchRevision = preparing->m_Revision;
		const auto transitionFrame = control.CaptureFrameState(probe.m_ParameterBuiltSerial, 0, 0);
		const auto requestedRadius = std::ranges::find(transitionFrame.m_Values,
			std::string("radius"), &ParameterFrameValue::m_Id);
		checks.Check(!transitionFrame.m_Available && requestedRadius != transitionFrame.m_Values.end() &&
			std::get<float>(requestedRadius->m_Value) == 2.0f && probe.m_ParameterBuiltRadius == 1.0f,
			"Closed edit admission does not erase the transition frame's requested candidate values");
		checks.Check(preparing->m_Items.size() == (protectedAssignments ? 4 : 5) &&
			generation.m_Domain == "lab-session" && generation.m_Generation != 0 &&
			preparing->m_Items[0].m_Applied && !preparing->m_Items[0].m_Published &&
			!preparing->m_Items[3].m_FirstConsumed,
			"Rebuild and Lab Immediate receipts await their candidate activation");
		// Protected Assign excludes Host entries; never index its four-item receipt
		// vector as the legacy five-item mixed batch, even if spare capacity exists.
		if (protectedAssignments)
		{
			checks.Check(probe.m_ParameterBuiltView.m_Exposure.m_ManualEV100 == 2.0f,
				"Protected Rebuild preserves the separately committed Host exposure on old-session frames");
		}
		else
		{
			checks.Check(preparing->m_Items.size() == 5 && preparing->m_Items[4].m_FirstConsumed,
				"Mixed Host Immediate consumption uses its actual old-session frame");
		}
		GGLAB_UNUSED(runtime.Tick());
		checks.Check(probe.m_ParameterSessionCreations == created + 1 &&
			control.GetContentIdentity() == originalIdentity && !control.GetParameterBatchStatus()->IsTerminal(),
			"Repeated old-session frames neither restart the operation nor consume its candidate");
		probe.m_HoldParameterPreparation = false;
		probe.m_SkipParameterFrame = true;
		GGLAB_UNUSED(runtime.Tick());
		const auto* published = control.GetParameterBatchStatus();
		checks.Check(published && !published->IsTerminal() && published->m_Revision == batchRevision &&
			published->m_Items[0].m_Identity == generation && published->m_Items[0].m_Published &&
			!published->m_Items[0].m_FirstConsumed && control.GetContentIdentity() != originalIdentity &&
			probe.m_ParameterBuiltView.m_Exposure.m_ManualEV100 == 2.0f,
			"Owned activation continues the batch and Host overrides; an aborted target frame proves publication only");
		const auto publishedFrame = published->m_Items[0].m_Published;
		GGLAB_UNUSED(runtime.Tick());
		const auto consumed = runtime.TakeParameterBatchResult(batchId);
		checks.Check(consumed && consumed->m_State == ParameterBatchState::Finished &&
			std::ranges::all_of(consumed->m_Items, [](const auto& item)
			{
				return item.m_State == ParameterEffectState::Consumed;
			}) && consumed->m_Items[0].m_Published == publishedFrame &&
			consumed->m_Items[0].m_FirstConsumed == probe.m_ParameterBuiltSerial &&
			consumed->m_Items[0].m_FirstConsumed > publishedFrame &&
			probe.m_ParameterSessionCreations == created + 1 && probe.m_ParameterBuiltRadius == 2.0f,
			"Retry consumption matches the target operation and retains first publication without recreating the session");
		if (protectedAssignments)
		{
			checks.Check(consumed && consumed->m_DomainOperation && consumed->m_DomainOperation->m_Committed &&
				consumed->m_DomainOperation->m_Before->m_ContentIdentity == originalIdentity &&
				consumed->m_DomainOperation->m_After->m_ContentIdentity == originalIdentity &&
				runtime.GetDomainControlStamp(ParameterControlDomain::Lab)->m_ContentIdentity != originalIdentity &&
				!runtime.RequestParameters(rebuildIntent, error) && error.starts_with("ExpectedContentChanged:"),
				"Rebuild commits under the source guard, consumes its owned target and requires a fresh target guard");
		}
		for (const auto name : { "scene-layout", "pipeline-layout", "layout" })
		{
			const auto id = request(name, 3);
			GGLAB_UNUSED(runtime.Tick());
			GGLAB_UNUSED(runtime.Tick());
			const auto result = runtime.TakeParameterBatchResult(id);
			checks.Check(result && result->m_Items[0].m_State == ParameterEffectState::Consumed &&
				result->m_Items[0].m_FirstConsumed == probe.m_ParameterBuiltSerial,
				"Each Rebuild impact follows the owned session transition and matching successful submission");
		}
		probe.m_HoldParameterPreparation = true;
		const auto failedId = request("layout", 4);
		GGLAB_UNUSED(runtime.Tick());
		probe.m_FailParameterPreparation = true;
		probe.m_FrameBeginUnavailable = true;
		GGLAB_UNUSED(runtime.Tick());
		const auto failed = runtime.TakeParameterBatchResult(failedId);
		checks.Check(failed && failed->m_State == ParameterBatchState::Failed &&
			!failed->m_Items[0].m_Published && !failed->m_Items[0].m_FirstConsumed &&
			lab.GetActiveSession()->GetParameters().Get(LabParameterId("layout"), uint32_t(0)) == 3,
			"Preparation failure closes the receipt without an RHI frame and preserves the old active Lab");
		probe.m_FailParameterPreparation = false;
		probe.m_FrameBeginUnavailable = false;
		probe.m_FailParameterCreation = true;
		const auto creationId = request("layout", 4);
		GGLAB_UNUSED(runtime.Tick());
		const auto creationFailure = runtime.TakeParameterBatchResult(creationId);
		checks.Check(creationFailure && creationFailure->m_State == ParameterBatchState::Failed &&
			!creationFailure->m_Items[0].m_FirstConsumed && !lab.HasPendingSession() &&
			lab.GetActiveSession()->GetParameters().Get(LabParameterId("layout"), uint32_t(0)) == 3,
			"Candidate factory failure is an explicit failed effect and leaves the old Lab intact");
		probe.m_FailParameterCreation = false;
		const auto supersededId = request("layout", 5);
		GGLAB_UNUSED(runtime.Tick());
		probe.m_HoldParameterPreparation = false;
		probe.m_FrameBeginUnavailable = true;
		lab.RequestRestartSession();
		GGLAB_UNUSED(runtime.Tick());
		const auto superseded = runtime.TakeParameterBatchResult(supersededId);
		checks.Check(superseded && superseded->m_Items[0].m_State == ParameterEffectState::Superseded &&
			!superseded->m_Items[0].m_FirstConsumed,
			"An unrelated restart cannot inherit the operation even when no renderer frame is available");
		probe.m_FrameBeginUnavailable = false;
		probe.m_HoldParameterPreparation = true;
		const auto shutdownId = request("layout", 6);
		GGLAB_UNUSED(runtime.Tick());
		runtime.Shutdown();
		const auto stopped = runtime.TakeParameterBatchResult(shutdownId);
		checks.Check(stopped && stopped->m_Items[0].m_State == ParameterEffectState::EffectNotReached &&
			!stopped->m_Items[0].m_FirstConsumed, "Shutdown closes the pending operation without invented consumption");
	}

	void RunContentBindingScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool failBeforeShutdown)
	{
		LifetimeProbe probe;
		probe.m_EnableParameterLab = true;
		probe.m_EnableLightingProfiles = true;
		// The Demo owns the nested content identity; it need not equal the raw Lab serial.
		probe.m_ContentSerialOffset = 100;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		// Retain this borrow: subsequent owner transitions must not need a fresh getter.
		auto& parameters = *runtime.GetParameterControl();
		runtime.EnableParameterControl();
		probe.m_ParameterControl = &parameters;
		auto& demos = *runtime.GetDemoManager();
		checks.Check(parameters.GetContentIdentity() == demos.GetActiveContentIdentity() &&
			probe.m_PlannedContentIdentity == parameters.GetContentIdentity(),
			"Parameter binding and frame planning share the Demo owner's nested content identity");
		const std::array overrideValues
		{
			LabParameterValue{ LabParameterId("view.gtao.radius"), 3.0f },
			LabParameterValue{ LabParameterId("radius"), 2.0f },
		};
		std::string error;
		const auto initial = runtime.RequestParameters(AssignmentRequest(runtime, overrideValues), error);
		GGLAB_UNUSED(runtime.Tick());
		checks.Check(initial != 0 && runtime.TakeParameterBatchResult(initial).has_value() &&
			probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius == 3.0f,
			"The binding fixture establishes an explicit Host override before content replacement");
		const std::array pendingValues
		{
			LabParameterValue{ LabParameterId("view.gtao.radius"), 4.0f },
			LabParameterValue{ LabParameterId("radius"), 2.5f },
		};
		const auto oldIdentity = parameters.GetContentIdentity();
		const auto staleLab = runtime.RequestParameters(AssignmentRequest(runtime, pendingValues), error);
		probe.m_ParameterRuntime->RequestRestartSession();
		probe.m_FrameBeginUnavailable = true;
		const auto readyFrames = probe.m_FrameBegins;
		GGLAB_UNUSED(runtime.Tick());
		const auto labResult = runtime.TakeParameterBatchResult(staleLab);
		checks.Check(staleLab != 0 && labResult && labResult->IsTerminal() && labResult->m_Items.empty() &&
			parameters.GetContentIdentity() != oldIdentity &&
			parameters.GetContentIdentity() == demos.GetActiveContentIdentity() && probe.m_FrameBegins == readyFrames &&
			parameters.GetHostParameters().Get(LabParameterId("view.gtao.radius"), 0.0f) == 2.0f,
			"Lab replacement cancels the stale batch and clears Host overrides during an Unavailable frame");
		auto* session = probe.m_ParameterRuntime->GetActiveSession();
		GGLAB_UNUSED(session->SetParameter(LabParameterId("radius"), 2.75f));
		session->ApplyParameterChanges(LabChangeImpact::Immediate);
		const auto beforeQuery = parameters.GetParameterRevision();
		checks.Check(runtime.GetParameterControl() == &parameters &&
			static_cast<const GGLabAppRuntime&>(runtime).GetParameterControl() == &parameters &&
			parameters.GetParameterRevision() == beforeQuery &&
			parameters.GetHostParameters().Get(LabParameterId("view.gtao.radius"), 0.0f) == 2.0f,
			"The parameter getter is a pure const accessor, without settings refresh or revision mutation");
		GGLAB_UNUSED(runtime.Tick());
		checks.Check(parameters.GetParameterRevision() > beforeQuery &&
			parameters.GetHostParameters().Get(LabParameterId("view.gtao.radius"), 0.0f) == 2.75f,
			"The explicit content update stage refreshes inherited settings even without RHI readiness");
		probe.m_FrameBeginUnavailable = false;
		const auto restoredOverride = runtime.RequestParameters(AssignmentRequest(runtime, overrideValues), error);
		GGLAB_UNUSED(runtime.Tick());
		checks.Check(restoredOverride != 0 && runtime.TakeParameterBatchResult(restoredOverride).has_value() &&
			probe.m_PlannedContentIdentity == parameters.GetContentIdentity(),
			"The next Ready frame plans with the same refreshed identity as parameter control");
		const auto pendingDemo = runtime.RequestParameters(AssignmentRequest(runtime, pendingValues), error);
		const auto oldDestructions = probe.m_DemoDestructions;
		const auto oldFrames = probe.m_FrameBegins;
		probe.m_ExpectedReleasedBatch = pendingDemo;
		probe.m_FrameBeginUnavailable = true;
		probe.m_HoldCaptureFence = true;
		demos.RequestActiveDemo(1);
		GGLAB_UNUSED(runtime.Tick());
		checks.Check(pendingDemo != 0 && probe.m_BindingReleasedBeforeExit &&
			demos.GetRetiringDemoCount() == 1 && probe.m_DemoDestructions == oldDestructions &&
			parameters.GetHostParameters().Get(LabParameterId("view.gtao.radius"), 0.0f) == 1.0f &&
			parameters.GetContentIdentity() == demos.GetActiveContentIdentity(),
			"Demo ownership cancels the old reservation and releases bindings before OnExit, independent of queries");
		GGLAB_UNUSED(runtime.Tick());
		probe.m_HoldCaptureFence = false;
		probe.m_ReadbackFence = {};
		GGLAB_UNUSED(runtime.Tick());
		checks.Check(demos.GetRetiringDemoCount() == 0 && probe.m_DemoDestructions == oldDestructions + 1 &&
			probe.m_FrameBegins == oldFrames && runtime.TakeParameterBatchResult(pendingDemo).has_value(),
			"The old Demo is destroyed behind its completed fence during repeated Unavailable frames without a control getter");
		const auto shutdownBatch = runtime.RequestParameters(AssignmentRequest(runtime, pendingValues), error);
		checks.Check(shutdownBatch != 0, "The current content accepts a new batch before teardown");
		if (failBeforeShutdown)
		{
			probe.m_FrameBeginUnavailable = false;
			probe.m_FrameEndInjection = FrameEndInjection::FatalFenced;
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal,
				"Runtime failure terminates the new binding after the previous owner has been destroyed");
		}
		runtime.Shutdown();
		const auto finished = runtime.TakeParameterBatchResult(shutdownBatch);
		checks.Check(finished && finished->IsTerminal() &&
			std::ranges::none_of(finished->m_Items, [](const auto& item) { return item.m_FirstConsumed.has_value(); }) &&
			parameters.GetContentIdentity() == 0 && !parameters.GetLabParameters() &&
			parameters.GetHostParameters().CaptureValues().empty(),
			"Shutdown clears content borrows and overrides without revisiting the destroyed owner or inventing consumption");
	}
}

namespace gglab
{
	using namespace runtime_test;

	void RunContentLifetimeIntegrationSelfTests(SelfTestContext& context) noexcept
	{
		RuntimeTestDirectory directory("content");
		const auto& root = directory.GetPath();
		RunContentBindingScenario(context, root / "content-binding-shutdown", false);
		RunContentBindingScenario(context, root / "content-binding-failure", true);
		RunLabSessionRetirementScenario(context, root / "lab-session-retirement");
		RunRebuildParameterScenario(context, root / "parameter-rebuild");
		RunRebuildParameterScenario(context, root / "protected-rebuild", true);
	}
}
