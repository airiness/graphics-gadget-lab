#include "ApplicationInput.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Demo/DemoManager.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Graphics/RenderCompositionAccess.h"
#include "GGLabRuntime/Graphics/WorldLighting.h"
#include "GGLabRuntime/Scene/Components.h"
#include "GGLabTestCore/SelfTest.h"
#include "GpuFaultLifecycleSelfTests.h"
#include "Lab/LabParameter.h"
#include "Lab/LabRuntime.h"
#include "Lab/LabTypes.h"
#include "Parameters/ParameterBatch.h"
#include "Parameters/ParameterEffectReceipt.h"
#include "Parameters/ParameterRequest.h"
#include "RuntimeIntegrationTestCases.h"
#include "RuntimeIntegrationTestServices.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <vector>

namespace gglab::runtime_test
{
	void RunInheritedLightingScenario(SelfTestContext& checks, const std::filesystem::path& root)
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
		const LabId labId("gglab.lab.test.inherited-lighting");
		const LabParameterId samples("samples");
		checks.Check(lab.RegisterLab({ .m_Id = labId, .m_DisplayName = "Inherited lighting fixture" },
			&CreateInheritedLightingTestLab) && lab.Initialize(labId),
			"The inherited lighting fixture initializes through the production LabRuntime");
		lab.TickTransitions();
		lab.OnEnter();
		const auto settings = [&]()
			{
				return probe.m_ContentServices.m_EnvironmentLighting->GetEnvironmentLightingSettings();
			};
		const auto generation = [&]()
			{
				return probe.m_ContentServices.m_RenderServices.m_Environment->GetBakeRequestGeneration();
			};
		const auto baseline = settings().m_BakeConfig.m_PrefilteredSpecularSampleCount;
		lab.RequestSetParameter(samples, uint32_t(64));
		lab.ProcessPendingCommands();
		checks.Check(settings().m_BakeConfig.m_PrefilteredSpecularSampleCount == 64,
			"An entered Lab establishes its explicit Runtime override");
		probe.m_HoldLightingPreparation = true;
		lab.RequestSwitchLab(labId);
		const auto beforePrepare = generation();
		lab.ProcessPendingCommands();
		checks.Check(lab.HasPendingSession() && generation() == beforePrepare &&
			settings().m_BakeConfig.m_PrefilteredSpecularSampleCount == 64,
			"Preparing replacement content leaves the active owner's lighting untouched");
		probe.m_HoldLightingPreparation = false;
		lab.TickTransitions();
		checks.Check(lab.IsReady() && settings().m_BakeConfig.m_PrefilteredSpecularSampleCount == baseline &&
			lab.GetActiveSession()->GetParameters().Get(samples, uint32_t(0)) == baseline &&
			!lab.GetActiveSession()->GetParameters().Find(samples)->m_Dirty && generation() == beforePrepare + 1,
			"Replacement inherits after the old owner restores, without reapplying a stale pending default");
		lab.RequestSetParameter(samples, uint32_t(128));
		lab.ProcessPendingCommands();
		lab.RequestRestartSession();
		lab.ProcessPendingCommands();
		checks.Check(settings().m_BakeConfig.m_PrefilteredSpecularSampleCount == 128 &&
			lab.GetActiveSession()->GetParameters().Find(samples)->m_Dirty &&
			std::get<uint32_t>(lab.GetActiveSession()->GetParameters().Find(samples)->m_Desc.m_DefaultValue) == baseline,
			"Restart preserves explicit intent while rebinding its entered reset default");
		probe.m_ParameterEnvironment->SetPrefilteredSpecularSampleCount(256);
		const auto externalGeneration = generation();
		const auto revision = lab.GetParameterRevision();
		lab.RequestSetParameter(LabParameterId("toggle"), true);
		lab.ProcessPendingCommands();
		checks.Check(lab.GetParameterRevision() > revision && generation() == externalGeneration &&
			lab.GetActiveSession()->GetParameters().Get(samples, uint32_t(0)) == 256 &&
			!lab.GetActiveSession()->GetParameters().Find(samples)->m_Dirty,
			"A later ordinary writer is reflected before edits and is not replayed by unrelated Lab effects");
		lab.RequestRestartSession();
		lab.ProcessPendingCommands();
		checks.Check(generation() == externalGeneration &&
			lab.GetActiveSession()->GetParameters().Get(samples, uint32_t(0)) == 256 &&
			!lab.GetActiveSession()->GetParameters().Find(samples)->m_Dirty,
			"Restart leaves an inherited value inherited and schedules no redundant bake");
		probe.m_HoldLightingPreparation = true;
		lab.RequestRestartSession();
		lab.ProcessPendingCommands();
		lab.Shutdown();
		checks.Check(generation() == externalGeneration && settings().m_BakeConfig.m_PrefilteredSpecularSampleCount == 256,
			"Cancelling a pending session and exiting preserves the later writer's lighting");
		probe.m_HoldLightingPreparation = false;
		const std::array startup{ LabParameterAssignment{ "samples", "512" } };
		checks.Check(lab.Initialize(labId, startup), "The lighting fixture accepts an explicit startup placeholder value");
		lab.TickTransitions();
		lab.OnEnter();
		checks.Check(settings().m_BakeConfig.m_PrefilteredSpecularSampleCount == 512 &&
			lab.GetActiveSession()->GetParameters().Find(samples)->m_Dirty,
			"Explicit startup equal to the constructor placeholder wins over the actual entered baseline");
		lab.Shutdown();
		checks.Check(settings().m_BakeConfig.m_PrefilteredSpecularSampleCount == 256,
			"The explicit startup override restores its actual entered baseline on exit");
		runtime.Shutdown();
	}

	void RunLightingParameterScenario(SelfTestContext& checks, const std::filesystem::path& root)
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
		auto& parameters = *runtime.GetParameterControl();
		probe.m_ParameterControl = &parameters;
		std::string error;
		const auto request = [&](uint32_t value, bool mixed = false)
			{
				std::vector<LabParameterValue> values{ { LabParameterId("lighting"), value } };
				if (mixed)
				{
					values.push_back({ LabParameterId("radius"), 2.0f });
					values.push_back({ LabParameterId("view.exposure.ev100"), 8.0f });
				}
				return runtime.RequestParameters(AssignmentRequest(runtime, values), error);
			};
		const auto tickUntil = [&](auto condition)
			{
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
				while (!condition() && std::chrono::steady_clock::now() < deadline)
				{
					// This fault backend deliberately holds cache-readback fences. This
					// success scenario explicitly delivers completion so a later bake can start.
					probe.m_ReadbackFence = {};
					if (runtime.Tick() != AppRuntimeTickResult::Continue)
					{
						return false;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				return condition();
			};
		const auto& environment = *probe.m_ContentServices.m_RenderServices.m_Environment;
		checks.Check(tickUntil([&]() { return environment.GetBakingStatus().m_ActiveGeneration != 0; }),
			"Deferred fixture establishes an actual published production bake before its experiment");
		const std::array tooMany{ LabParameterValue{ LabParameterId("lighting"), uint32_t(2) },
			LabParameterValue{ LabParameterId("broken-lighting"), uint32_t(2) } };
		checks.Check(runtime.RequestParameters(AssignmentRequest(runtime, tooMany), error) == 0 &&
			!parameters.GetParameterBatchStatus(),
			"Multiple lighting bindings reject atomically before any owner handler runs");
		const auto applications = probe.m_ParameterApplications;
		const auto id = request(2, true);
		GGLAB_UNUSED(runtime.Tick());
		const auto first = *parameters.GetParameterBatchStatus();
		checks.Check(id != 0 && first.m_Items.size() == 3 && !first.IsTerminal() &&
			first.m_Items[0].m_State == ParameterEffectState::Applied && !first.m_Items[0].m_Published &&
			first.m_Items[1].m_State == ParameterEffectState::Consumed &&
			first.m_Items[2].m_State == ParameterEffectState::Consumed &&
			first.m_Items[0].m_Identity.m_Domain == "lighting" &&
			first.m_Items[0].m_Identity.m_Generation == environment.GetBakeRequestGeneration(),
			"A mixed real frame consumes Immediate state while Deferred state keeps its own applied generation pending");
		checks.Check(!runtime.TakeParameterBatchResult(id) && request(3) == 0,
			"Pending Deferred effects retain their one result slot and cannot be consumed early");
		checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }),
			"The production scheduler eventually publishes the exact Deferred generation");
		const auto done = runtime.TakeParameterBatchResult(id);
		checks.Check(done && done->m_Items[0].m_State == ParameterEffectState::Consumed &&
			done->m_Items[0].m_Applied == first.m_Items[0].m_Applied &&
			done->m_Items[0].m_Published > done->m_Items[0].m_Applied &&
			done->m_Items[0].m_FirstConsumed == done->m_Items[0].m_Published &&
			probe.m_ParameterApplications == applications + 1,
			"Deferred stamps distinguish application from actual publication and successful first consumption without reapplying");
		const auto generation = environment.GetBakeRequestGeneration();
		const auto revision = parameters.GetParameterRevision();
		const auto nochange = request(2);
		GGLAB_UNUSED(runtime.Tick());
		const auto unchanged = runtime.TakeParameterBatchResult(nochange);
		checks.Check(unchanged && unchanged->m_Items[0].m_State == ParameterEffectState::NoChange &&
			!unchanged->m_Items[0].m_Published && !unchanged->m_Items[0].m_FirstConsumed &&
			parameters.GetParameterRevision() == revision && environment.GetBakeRequestGeneration() == generation &&
			probe.m_ParameterApplications == applications + 1,
			"Deferred NoChange confirms the active target without a new request, Immediate handler, publication or revision");
		const auto lightingCalls = probe.m_LightingParameterApplications;
		const std::array immediate{ LabParameterValue{ LabParameterId("radius"), 2.5f } };
		const auto immediateId = runtime.RequestParameters(AssignmentRequest(runtime, immediate), error);
		GGLAB_UNUSED(runtime.Tick());
		const auto immediateResult = runtime.TakeParameterBatchResult(immediateId);
		checks.Check(immediateResult && immediateResult->m_State == ParameterBatchState::Finished &&
			probe.m_LightingParameterApplications == lightingCalls &&
			environment.GetBakeRequestGeneration() == generation,
			"An unrelated Immediate batch never calls the Deferred binding or reuses its previous result");

		probe.m_ParameterEnvironment->SetPrefilteredSpecularSampleCount(3);
		const auto existingGeneration = environment.GetBakeRequestGeneration();
		const auto existingId = request(3);
		GGLAB_UNUSED(runtime.Tick());
		const auto existing = *parameters.GetParameterBatchStatus();
		checks.Check(existingId != 0 && !existing.IsTerminal() &&
			existing.m_Items[0].m_State == ParameterEffectState::Applied &&
			existing.m_Items[0].m_Identity.m_Generation == existingGeneration &&
			environment.GetBakeRequestGeneration() == existingGeneration,
			"A changed source value binds an existing pending Runtime target without issuing another request");
		checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }),
			"An existing pending target reaches real publication and submitted consumption");
		const auto existingDone = runtime.TakeParameterBatchResult(existingId);
		checks.Check(existingDone && existingDone->m_Items[0].m_State == ParameterEffectState::Consumed &&
			existingDone->m_Items[0].m_FirstConsumed && environment.GetBakeRequestGeneration() == existingGeneration,
			"Binding an existing target retains honest Deferred publication and first-consumption receipts");

		lab.RequestSetParameter(LabParameterId("lighting"), uint32_t(4));
		GGLAB_UNUSED(runtime.Tick());
		const auto pendingGeneration = environment.GetBakeRequestGeneration();
		const auto pendingRevision = parameters.GetParameterRevision();
		const auto pendingId = request(4);
		GGLAB_UNUSED(runtime.Tick());
		checks.Check(pendingId != 0 && !parameters.GetParameterBatchStatus()->IsTerminal() &&
			parameters.GetParameterBatchStatus()->m_Items[0].m_Identity.m_Generation == pendingGeneration &&
			parameters.GetParameterRevision() == pendingRevision &&
			environment.GetBakeRequestGeneration() == pendingGeneration,
			"Unchanged parameter storage cannot report NoChange while its actual target is pending");
		checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }),
			"An unchanged pending target eventually terminates through the production scheduler");
		const auto pendingDone = runtime.TakeParameterBatchResult(pendingId);
		checks.Check(pendingDone && pendingDone->m_Items[0].m_State == ParameterEffectState::Consumed,
			"An unchanged pending target is consumed with no new revision or generation");

		probe.m_ReturnStaleLightingEvidence = true;
		const auto staleId = request(4);
		GGLAB_UNUSED(runtime.Tick());
		const auto stale = runtime.TakeParameterBatchResult(staleId);
		checks.Check(stale && stale->m_State == ParameterBatchState::Failed &&
			stale->m_Failure.find("LightingEffectUnproven") != std::string::npos,
			"A stale earlier request is rejected even for unchanged parameter storage");
		probe.m_ReturnMissingLightingEvidence = true;
		const auto missingId = request(4);
		GGLAB_UNUSED(runtime.Tick());
		const auto missing = runtime.TakeParameterBatchResult(missingId);
		checks.Check(missing && missing->m_State == ParameterBatchState::Failed &&
			missing->m_Failure.find("LightingEffectUnproven") != std::string::npos,
			"A declared capability without this operation's evidence cannot confirm an unchanged target");

		probe.m_RequestLightingTwice = true;
		const auto beforeDouble = environment.GetBakeRequestGeneration();
		const auto doubleId = request(5);
		GGLAB_UNUSED(runtime.Tick());
		checks.Check(doubleId != 0 && !parameters.GetParameterBatchStatus()->IsTerminal() &&
			parameters.GetParameterBatchStatus()->m_Items[0].m_State == ParameterEffectState::Applied &&
			parameters.GetParameterBatchStatus()->m_Items[0].m_Identity.m_Generation == beforeDouble + 2,
			"Session accepts the target returned by one owning operation without assuming generation plus one");
		checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }),
			"A target following multiple domain requests reaches actual publication");
		const auto doubleDone = runtime.TakeParameterBatchResult(doubleId);
		checks.Check(doubleDone && doubleDone->m_Items[0].m_State == ParameterEffectState::Consumed,
			"The exact final target from a multiple-request operation is consumed");

		const auto replacedId = request(3);
		GGLAB_UNUSED(runtime.Tick());
		lab.RequestSetParameter(LabParameterId("lighting"), uint32_t(4));
		GGLAB_UNUSED(runtime.Tick());
		const auto replaced = runtime.TakeParameterBatchResult(replacedId);
		checks.Check(replaced && replaced->m_Items[0].m_State == ParameterEffectState::Superseded &&
			!replaced->m_Items[0].m_Published && !replaced->m_Items[0].m_FirstConsumed,
			"A later UI lighting request supersedes an unpublished exact generation without consuming its replacement");
		const auto retryId = request(5);
		probe.m_SkipLightingPublication = true;
		checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->m_Items.size() == 1 &&
			parameters.GetParameterBatchStatus()->m_Items[0].m_Published.has_value(); }),
			"A publication can be observed in a frame subsequently aborted by the real RAII path");
		const auto aborted = *parameters.GetParameterBatchStatus();
		checks.Check(!aborted.IsTerminal() && !aborted.m_Items[0].m_FirstConsumed,
			"An aborted publication frame is never Deferred firstConsumed");
		probe.m_RebakeAfterParameterBuild = true;
		GGLAB_UNUSED(runtime.Tick());
		const auto retried = runtime.TakeParameterBatchResult(retryId);
		checks.Check(retried && retried->m_Items[0].m_State == ParameterEffectState::Consumed &&
			retried->m_Items[0].m_Published == aborted.m_Items[0].m_Published &&
			retried->m_Items[0].m_FirstConsumed > retried->m_Items[0].m_Published &&
			environment.GetBakeRequestGeneration() != retried->m_Items[0].m_Identity.m_Generation,
			"Retry consumes the frozen built lighting generation even when a later live request advances after build");
		const auto supersededAfterPublication = request(6);
		probe.m_SkipLightingPublication = true;
		checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->m_Items.size() == 1 &&
			parameters.GetParameterBatchStatus()->m_Items[0].m_Published.has_value(); }),
			"Deferred publication is retained before a later UI replacement");
		const auto oldPublished = parameters.GetParameterBatchStatus()->m_Items[0].m_Published;
		lab.RequestSetParameter(LabParameterId("lighting"), uint32_t(7));
		GGLAB_UNUSED(runtime.Tick());
		const auto overwritten = runtime.TakeParameterBatchResult(supersededAfterPublication);
		checks.Check(overwritten && overwritten->m_Items[0].m_State == ParameterEffectState::Superseded &&
			overwritten->m_Items[0].m_Published == oldPublished && !overwritten->m_Items[0].m_FirstConsumed,
			"A published but aborted Deferred state keeps publication evidence when superseded before consumption");
		const std::array broken{ LabParameterValue{ LabParameterId("broken-lighting"), uint32_t(2) },
			LabParameterValue{ LabParameterId("radius"), 3.0f } };
		const auto brokenId = runtime.RequestParameters(AssignmentRequest(runtime, broken), error);
		GGLAB_UNUSED(runtime.Tick());
		const auto failed = runtime.TakeParameterBatchResult(brokenId);
		checks.Check(failed && failed->m_State == ParameterBatchState::Failed &&
			std::ranges::all_of(failed->m_Items, [](const auto& item) { return item.IsTerminal(); }) &&
			failed->m_Failure.find("LightingEffectUnproven") != std::string::npos,
			"Unproven domain attribution closes every pending sibling instead of returning a terminal batch with unfinished items");
		const std::array unrelated{ LabParameterValue{ LabParameterId("lighting"), uint32_t(7) },
			LabParameterValue{ LabParameterId("rebake"), true } };
		const auto unrelatedId = runtime.RequestParameters(AssignmentRequest(runtime, unrelated), error);
		GGLAB_UNUSED(runtime.Tick());
		const auto unattributed = runtime.TakeParameterBatchResult(unrelatedId);
		checks.Check(unattributed && unattributed->m_State == ParameterBatchState::Failed &&
			unattributed->m_Failure.find("LightingEffectUnproven") != std::string::npos &&
			std::ranges::none_of(unattributed->m_Items, [](const auto& item) { return item.m_FirstConsumed.has_value(); }),
			"An unchanged Deferred binding cannot absorb an unrelated Immediate handler's lighting request");
		const auto fatalId = request(8);
		probe.m_FrameEndInjection = FrameEndInjection::FatalFenced;
		checks.Check(fatalId != 0 && runtime.Tick() == AppRuntimeTickResult::Fatal &&
			parameters.GetParameterBatchStatus()->IsTerminal() && !parameters.GetParameterBatchStatus()->m_Items[0].m_FirstConsumed,
			"Fatal submission closes an applied Deferred wait without inventing publication or consumption");
		runtime.Shutdown();
	}

	void RunLightingProfileRegistrationScenario(SelfTestContext& checks,
		const std::filesystem::path& root, bool labBoolean, bool equivalent)
	{
		LifetimeProbe probe;
		probe.m_EnableParameterLab = labBoolean;
		probe.m_ProfileNamedLabBool = labBoolean;
		probe.m_EnableLightingProfiles = !labBoolean;
		probe.m_InvalidLightingProfiles = !labBoolean && !equivalent;
		probe.m_EquivalentLightingProfiles = equivalent;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		auto& parameters = *runtime.GetParameterControl();
		runtime.EnableParameterControl();
		std::string error;
		if (labBoolean)
		{
			const std::array values{ LabParameterValue{ LabParameterId("environment.profile"), true } };
			const auto id = runtime.RequestParameters(AssignmentRequest(runtime, values), error);
			GGLAB_UNUSED(runtime.Tick());
			const auto result = runtime.TakeParameterBatchResult(id);
			checks.Check(result && result->m_State == ParameterBatchState::Finished &&
				probe.m_ParameterRuntime->GetActiveSession()->GetParameters().Get(
					LabParameterId("environment.profile"), false),
				"A Lab-owned environment.profile boolean dispatches through its source, not the Host enum variant");
		}
		else
		{
			checks.Check(parameters.IsAvailable() && parameters.GetLightingProfiles().empty() &&
				parameters.GetLightingProfileError().starts_with("LightingProfileRegistration:"),
				"Invalid or effective-equivalent registrations report a configuration error without disabling Host controls");
			const auto queries = probe.m_LightingProfileQueries;
			const std::array invalid{ LabParameterValue{ LabParameterId("environment.profile"), int32_t(0) } };
			checks.Check(runtime.RequestParameters(AssignmentRequest(runtime, invalid), error) == 0 &&
				error.starts_with("LightingProfileRegistration:"),
				"Selecting an invalid registration reports its concrete error instead of source unreadiness");
			const std::array host{ LabParameterValue{ LabParameterId("view.gtao.radius"), 2.0f } };
			const auto id = runtime.RequestParameters(AssignmentRequest(runtime, host), error);
			GGLAB_UNUSED(runtime.Tick());
			const auto result = runtime.TakeParameterBatchResult(id);
			checks.Check(result && result->m_State == ParameterBatchState::Finished &&
				probe.m_LightingProfileQueries == queries,
				"Unrelated Host controls remain usable and invalid registrations are cached for the content lifetime");
		}
		runtime.Shutdown();
	}

	void RunLabWorldLightingScenario(SelfTestContext& checks, const std::filesystem::path& root)
	{
		LifetimeProbe probe;
		probe.m_EnableParameterLab = true;
		probe.m_EnableLightingProfiles = true;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		runtime.EnableParameterControl();
		auto& parameters = *runtime.GetParameterControl();
		probe.m_ParameterControl = &parameters;
		auto& lab = *probe.m_ParameterRuntime;
		auto& lighting = *runtime.GetRenderComposition()->GetWorldLightingControl();
		const auto& environment = *probe.m_ContentServices.m_RenderServices.m_Environment;
		const auto tickUntil = [&](auto condition)
			{
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
				while (!condition() && std::chrono::steady_clock::now() < deadline)
				{
					probe.m_ReadbackFence = {};
					if (runtime.Tick() != AppRuntimeTickResult::Continue)
					{
						return false;
					}
				}
				return condition();
			};
		checks.Check(tickUntil([&]() { return environment.GetBakingStatus().m_ActiveGeneration != 0; }),
			"The Lab World Lighting fixture starts from a real published generation");
		const std::array sun{ LabParameterValue{ LabParameterId("sun-input"), 2.0f } };
		std::string error;
		auto generation = environment.GetBakeRequestGeneration();
		const auto applications = probe.m_ParameterApplications;
		checks.Check(lab.QueueParameterBatch(100, sun, error) &&
			!lab.PrepareParameterBatch(100, sun, error),
			"A World Lighting Lab plan requires the Session's explicit control and content identity");
		auto prepared = lab.PrepareParameterBatch(100, sun, error, &lighting, parameters.GetContentIdentity());
		checks.Check(prepared && prepared->HasWorldLightingChanges() &&
			environment.GetBakeRequestGeneration() == generation && probe.m_ParameterApplications == applications &&
			lab.GetActiveSession()->GetParameters().Get(LabParameterId("sun-input"), 0.0f) == 1.0f,
			"Preparing a Lab sun plan mutates neither World Lighting, parameter storage nor callbacks");
		prepared.reset();
		lab.CancelParameterBatch(100);
		const std::array invalid{ LabParameterValue{ LabParameterId("view.exposure.ev100"), 11.0f },
			LabParameterValue{ LabParameterId("sun-input"), 99.0f } };
		checks.Check(runtime.RequestParameters(AssignmentRequest(runtime, invalid), error) == 0 &&
			environment.GetBakeRequestGeneration() == generation,
			"An invalid authored sun tail rejects the complete mixed batch");
		const std::array values{ LabParameterValue{ LabParameterId("sun-input"), 2.0f },
			LabParameterValue{ LabParameterId("radius"), 2.0f },
			LabParameterValue{ LabParameterId("view.exposure.ev100"), 11.0f } };
		auto& world = runtime.GetDemoManager()->GetActiveDemo()->GetWorld();
		std::optional<entt::entity> sunEntity;
		for (const auto entity : world.GetRegistry().view<components::LightComponent>())
		{
			if (world.GetRegistry().get<components::LightComponent>(entity).m_WorldSun)
			{
				sunEntity = entity;
				break;
			}
		}
		checks.Check(sunEntity.has_value(), "The prepared Lab fixture owns a designated World Sun");
		if (sunEntity)
		{
			const float exposure = parameters.GetHostParameters().Get(LabParameterId("view.exposure.ev100"), 0.0f);
			const auto missing = runtime.RequestParameters(AssignmentRequest(runtime, values), error);
			auto& light = world.GetRegistry().get<components::LightComponent>(*sunEntity);
			const auto authored = light.m_WorldSun;
			light.m_WorldSun.reset();
			lighting.SynchronizeWorldLighting(world, parameters.GetContentIdentity());
			const auto externalGeneration = environment.GetBakeRequestGeneration();
			GGLAB_UNUSED(runtime.Tick());
			const auto rejected = runtime.TakeParameterBatchResult(missing);
			checks.Check(rejected && rejected->m_State == ParameterBatchState::Failed && rejected->m_Items.empty() &&
				probe.m_ParameterApplications == applications && environment.GetBakeRequestGeneration() == externalGeneration &&
				parameters.GetHostParameters().Get(LabParameterId("view.exposure.ev100"), 0.0f) == exposure &&
				lab.GetActiveSession()->GetParameters().Get(LabParameterId("sun-input"), 0.0f) == 1.0f,
				"A Lab sun lost after admission rejects preparation without committing Lab, Host or another generation");
			light.m_WorldSun = authored;
			lighting.SynchronizeWorldLighting(world, parameters.GetContentIdentity());
			generation = environment.GetBakeRequestGeneration();
			checks.Check(tickUntil([&]() { return environment.GetBakingStatus().m_ActiveGeneration == generation; }),
				"The restored authored sun publishes before another Lab mutation is admitted");
		}
		bool completeCommit = false;
		probe.m_ParameterEffectCallback = [&]()
			{
				completeCommit = environment.GetBakeRequestGeneration() == generation + 1 &&
					parameters.GetHostParameters().Get(LabParameterId("view.exposure.ev100"), 0.0f) == 11.0f &&
					lab.GetActiveSession()->GetParameters().Get(LabParameterId("sun-input"), 0.0f) == 2.0f;
			};
		const auto id = runtime.RequestParameters(AssignmentRequest(runtime, values), error);
		GGLAB_UNUSED(runtime.Tick());
		checks.Check(id && completeCommit && parameters.GetParameterBatchStatus()->m_Items[0].m_Applied &&
			parameters.GetParameterBatchStatus()->m_Items[0].m_Identity.m_Generation == generation + 1,
			"World Lighting, Lab and Host commit completely before the first Lab callback observes the batch");
		if (!id || !parameters.GetParameterBatchStatus() || parameters.GetParameterBatchStatus()->m_Items.empty())
		{
			runtime.Shutdown();
			return;
		}
		probe.m_SkipLightingPublication = true;
		checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->m_Items[0].m_Published.has_value(); }) &&
			!parameters.GetParameterBatchStatus()->m_Items[0].m_FirstConsumed,
			"An aborted frame may observe prepared Lab sun publication without consuming it");
		GGLAB_UNUSED(runtime.Tick());
		const auto consumed = runtime.TakeParameterBatchResult(id);
		checks.Check(consumed && consumed->IsTerminal() && consumed->m_Items[0].m_FirstConsumed > consumed->m_Items[0].m_Published,
			"Only the successful retry consumes the exact Lab sun generation");
		const auto revision = parameters.GetParameterRevision();
		const auto unchangedGeneration = environment.GetBakeRequestGeneration();
		const auto same = runtime.RequestParameters(AssignmentRequest(runtime, values), error);
		GGLAB_UNUSED(runtime.Tick());
		const auto noChange = runtime.TakeParameterBatchResult(same);
		checks.Check(noChange && noChange->m_Items[0].m_State == ParameterEffectState::NoChange &&
			parameters.GetParameterRevision() == revision && environment.GetBakeRequestGeneration() == unchangedGeneration,
			"Repeating an owned Lab sun state is NoChange without a new revision or generation");
		const std::array next{ LabParameterValue{ LabParameterId("sun-input"), 3.0f } };
		const auto pending = runtime.RequestParameters(AssignmentRequest(runtime, next), error);
		GGLAB_UNUSED(runtime.Tick());
		GGLAB_UNUSED(lighting.ApplyWorldLightingInputs(runtime.GetDemoManager()->GetActiveDemo()->GetWorld(),
			parameters.GetContentIdentity(), parameters.GetLightingProfiles()[1].m_Inputs));
		GGLAB_UNUSED(runtime.Tick());
		const auto superseded = runtime.TakeParameterBatchResult(pending);
		checks.Check(superseded && superseded->m_Items[0].m_State == ParameterEffectState::Superseded &&
			!superseded->m_Items[0].m_FirstConsumed,
			"A foreign authored profile supersedes a pending Lab sun receipt without borrowing consumption");
		runtime.Shutdown();
	}

	void RunLightingProfileScenario(SelfTestContext& checks, const std::filesystem::path& root,
		bool protectedAssignments)
	{
		LifetimeProbe probe;
		probe.m_EnableLightingProfiles = true;
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
		{
			return;
		}
		auto& parameters = *runtime.GetParameterControl();
		probe.m_ParameterControl = &parameters;
		runtime.EnableParameterControl();
		const auto& environment = *probe.m_ContentServices.m_RenderServices.m_Environment;
		const auto tickUntil = [&](auto condition)
			{
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
				while (!condition() && std::chrono::steady_clock::now() < deadline)
				{
					probe.m_ReadbackFence = {};
					if (runtime.Tick() != AppRuntimeTickResult::Continue)
					{
						return false;
					}
				}
				return condition();
			};
		checks.Check(tickUntil([&]() { return environment.GetBakingStatus().m_ActiveGeneration != 0; }),
			"Profile fixture publishes its real initial Physical Sky generation");
		checks.Check(parameters.GetLightingProfiles().size() == 2 &&
			parameters.GetHostParameters().Find(LabParameterId("environment.profile")) != nullptr,
			"Only active content registers the bounded named profile source");
		std::string error;
		const auto request = [&](int32_t profile)
			{
				std::vector values{ LabParameterValue{ LabParameterId("environment.profile"), profile } };
				if (!protectedAssignments)
				{
					values.push_back({ LabParameterId("view.exposure.ev100"), 8.0f });
				}
				auto intent = AssignmentRequest(runtime, values);
				if (protectedAssignments)
				{
					const auto stamp = runtime.GetDomainControlStamp(ParameterControlDomain::LightingProfile);
					if (!stamp)
					{
						error = "Protected lighting profile control is unavailable.";
						return uint64_t(0);
					}
					intent.m_ExpectedDomain = *stamp;
				}
				return runtime.RequestParameters(intent, error);
			};
		if (protectedAssignments)
		{
			const std::array baseline{ LabParameterValue{ LabParameterId("view.exposure.ev100"), 8.0f } };
			const auto configured = runtime.RequestParameters(AssignmentRequest(runtime, baseline), error);
			GGLAB_UNUSED(runtime.Tick());
			checks.Check(runtime.TakeParameterBatchResult(configured).has_value(),
				"Protected profile workflow keeps exposure under its separate Host owner");
		}
		const auto initialRevision = parameters.GetParameterRevision();
		checks.Check(request(-1) == 0 && request(99) == 0 &&
			parameters.GetParameterRevision() == initialRevision && !parameters.GetParameterBatchStatus(),
			"Read-only custom and unknown profile enums reject the complete mixed batch");
		const auto id = request(1);
		GGLAB_UNUSED(runtime.Tick());
		const auto first = *parameters.GetParameterBatchStatus();
		if (protectedAssignments)
		{
			checks.Check(first.m_DomainOperation && first.m_DomainOperation->m_Committed &&
				first.m_DomainOperation->m_Before && first.m_DomainOperation->m_After,
				"Profile CAS commits once while its Deferred effect still waits for real consumption");
		}
		const auto state = parameters.GetLightingProfilePublication();
		const auto transitionFrame = parameters.CaptureFrameState(probe.m_ParameterBuiltSerial,
			environment.GetBakeRequestGeneration(), environment.GetBakingStatus().m_ActiveGeneration);
		const auto transitionBytes = SerializeParameterFrameState(transitionFrame);
		checks.Check(transitionFrame.m_RequestedLighting.m_Profile && transitionFrame.m_ActiveLighting.m_Profile &&
			transitionFrame.m_RequestedLighting.m_Profile->m_Id == "test.oblique" &&
			transitionFrame.m_ActiveLighting.m_Profile->m_Id == "test.baseline" &&
			transitionFrame.m_RequestedLighting.m_Generation != transitionFrame.m_ActiveLighting.m_Generation,
			"A transition snapshot freezes separately identified requested and active lighting generations");
		checks.Check(id != 0 && first.m_Items[0].m_State == ParameterEffectState::Applied &&
			first.m_Items[0].m_Identity.m_Generation == environment.GetBakeRequestGeneration() &&
			state.m_Requested && state.m_Requested->m_Id == "test.oblique" &&
			state.m_Active && state.m_Active->m_Id == "test.baseline" &&
			state.m_Requested->m_Generation != state.m_Active->m_Generation,
			"A real authored sun transition retains separate requested and active profile identities");
		checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }),
			"Profile publication reaches its exact submitted generation");
		const auto done = runtime.TakeParameterBatchResult(id);
		checks.Check(SerializeParameterFrameState(transitionFrame) == transitionBytes,
			"Later lighting publication and receipt collection cannot relabel a transition frame");
		checks.Check(done && done->m_Items[0].m_State == ParameterEffectState::Consumed &&
			done->m_Items[0].m_Published > done->m_Items[0].m_Applied &&
			done->m_Items[0].m_FirstConsumed == done->m_Items[0].m_Published &&
			probe.m_ParameterBuiltView.m_Exposure.m_ManualEV100 == 8.0f,
			"Profile receipts use actual publication/submission and never apply recommended exposure");
		const auto generation = environment.GetBakeRequestGeneration();
		const auto revision = parameters.GetParameterRevision();
		const auto same = request(1);
		GGLAB_UNUSED(runtime.Tick());
		const auto unchanged = runtime.TakeParameterBatchResult(same);
		checks.Check(unchanged && unchanged->m_Items[0].m_State == ParameterEffectState::NoChange &&
			parameters.GetParameterRevision() == revision && environment.GetBakeRequestGeneration() == generation,
			"Current profile NoChange causes no new request or revision");
		const auto queries = probe.m_LightingProfileQueries;
		auto& world = runtime.GetDemoManager()->GetActiveDemo()->GetWorld();
		for (const auto entity : world.GetRegistry().view<components::LightComponent>())
		{
			auto& light = world.GetRegistry().get<components::LightComponent>(entity);
			if (light.m_WorldSun)
			{
				auto& rotation = world.GetRegistry().get<components::TransformComponent>(entity).m_Rotation;
				rotation = Quaternion(-rotation.m_X, -rotation.m_Y, -rotation.m_Z, -rotation.m_W);
				light.m_WorldSun->m_Chromaticity *= 0.5f;
				world.MarkLightingAuthoringChanged();
			}
		}
		const auto equivalent = request(1);
		GGLAB_UNUSED(runtime.Tick());
		const auto equivalentResult = runtime.TakeParameterBatchResult(equivalent);
		checks.Check(equivalentResult && equivalentResult->m_Items[0].m_State == ParameterEffectState::NoChange &&
			environment.GetBakeRequestGeneration() == generation &&
			parameters.GetLightingProfilePublication().m_Requested->m_Generation == generation &&
			probe.m_LightingProfileQueries == queries,
			"Equivalent Quaternion/chromaticity authoring stays NoChange and never rebuilds static registrations or relabels a request");
		world.m_Atmosphere->m_RayleighScaleHeightMeters *= 2.0f;
		world.MarkLightingAuthoringChanged();
		const auto retry = request(0);
		probe.m_SkipLightingPublication = true;
		checks.Check(tickUntil([&]()
			{
				const auto* status = parameters.GetParameterBatchStatus();
				return !status->m_Items.empty() && status->m_Items[0].m_Published.has_value();
			}), "Profile publication may be observed on an aborted frame");
		const auto published = parameters.GetParameterBatchStatus()->m_Items[0].m_Published;
		auto abortedFrame = parameters.CaptureFrameState(*published,
			environment.GetBakeRequestGeneration(), environment.GetBakingStatus().m_ActiveGeneration);
		const auto abortedBytes = SerializeParameterFrameState(abortedFrame);
		checks.Check(!parameters.GetParameterBatchStatus()->m_Items[0].m_FirstConsumed &&
			parameters.GetParameterBatchStatus()->m_Items[0].m_Identity.m_Generation == generation + 2,
			"An unresolved authored edit settles before profile attribution, and aborted publication is not consumed");
		GGLAB_UNUSED(runtime.Tick());
		const auto retried = runtime.TakeParameterBatchResult(retry);
		parameters.CompleteFrameState(abortedFrame);
		checks.Check(SerializeParameterFrameState(abortedFrame) == abortedBytes &&
			!abortedFrame.m_Receipts.front().m_Item.m_FirstConsumed,
			"A later retry cannot lend consumption to an aborted frame snapshot");
		checks.Check(retried && retried->m_Items[0].m_Published == published &&
			retried->m_Items[0].m_FirstConsumed > published,
			"Retry preserves profile application/publication and supplies only first consumption");
		const auto supersededId = request(1);
		GGLAB_UNUSED(runtime.Tick());
		GGLAB_UNUSED(ApplyWorldLightingInputsToWorld(runtime.GetDemoManager()->GetActiveDemo()->GetWorld(),
			parameters.GetLightingProfiles()[0].m_Inputs));
		GGLAB_UNUSED(runtime.Tick());
		const auto superseded = runtime.TakeParameterBatchResult(supersededId);
		checks.Check(superseded && superseded->m_Items[0].m_State == ParameterEffectState::Superseded &&
			!superseded->m_Items[0].m_FirstConsumed,
			"An unrelated authored sun edit supersedes the selected profile instead of borrowing its generation");
		const auto selected = request(1);
		checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }) &&
			runtime.TakeParameterBatchResult(selected).has_value(),
			"A profile selection completes before exercising live content replacement");
		const auto oldContent = runtime.GetDemoManager()->GetTemporalSessionSerial();
		const auto oldGeneration = environment.GetBakeRequestGeneration();
		const auto cancelled = request(0);
		runtime.GetDemoManager()->RequestActiveDemo(1);
		GGLAB_UNUSED(runtime.Tick());
		runtime.EnableParameterControl();
		const auto replacedContent = runtime.TakeParameterBatchResult(cancelled);
		checks.Check(replacedContent && replacedContent->m_State == ParameterBatchState::Failed &&
			runtime.GetDemoManager()->GetTemporalSessionSerial() != oldContent &&
			parameters.GetLightingProfiles().size() == 2 &&
			environment.GetBakeRequestGeneration() > oldGeneration &&
			parameters.GetHostParameters().Get(LabParameterId("environment.profile"), int32_t(-1)) == 0,
			"A running content replacement cancels old ownership, rebuilds registrations and never retains the prior profile override");
		const auto replaced = request(1);
		GGLAB_UNUSED(runtime.Tick());
		runtime.Shutdown();
		checks.Check(parameters.GetParameterBatchStatus()->m_BatchId == replaced &&
			parameters.GetParameterBatchStatus()->IsTerminal() &&
			!parameters.GetParameterBatchStatus()->m_Items[0].m_FirstConsumed,
			"Shutdown closes profile ownership without fabricating consumption");
		checks.Check(parameters.GetLightingProfiles().empty() &&
			!parameters.GetLightingProfilePublication().m_Requested &&
			!parameters.GetLightingProfilePublication().m_Active,
			"Shutdown releases content bindings, profile registrations and both publication identities");
	}
}

namespace gglab
{
	using namespace runtime_test;

	void RunLightingParameterIntegrationSelfTests(SelfTestContext& context) noexcept
	{
		RuntimeTestDirectory directory("lighting");
		const auto& root = directory.GetPath();
		RunLightingParameterScenario(context, root / "lighting-parameters");
		RunLightingProfileRegistrationScenario(context, root / "profile-source-binding", true, false);
		RunLightingProfileRegistrationScenario(context, root / "profile-invalid-registration", false, false);
		RunLightingProfileRegistrationScenario(context, root / "profile-equivalent-registration", false, true);
		RunLightingProfileScenario(context, root / "lighting-profiles");
		RunLightingProfileScenario(context, root / "protected-lighting-profiles", true);
		RunLabWorldLightingScenario(context, root / "lab-world-lighting");
		RunInheritedLightingScenario(context, root / "inherited-lighting");
	}
}
