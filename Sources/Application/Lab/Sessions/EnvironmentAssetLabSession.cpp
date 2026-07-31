#include "Core/Precompiled.h"
#include "Application/Lab/Sessions/EnvironmentAssetLabSession.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Graphics/EnvironmentAssetController.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"

namespace gglab
{
	struct EnvironmentAssetLabSession::State
	{
		enum class Phase : uint8_t
		{
			WaitForInitialEnvironment,
			WaitForRapidSelection,
			WaitForTransactionalSwitch,
			WaitForDecodeFailure,
			WaitForInvalidShape,
			ObserveFallback,
			WaitForReselection,
			WaitForInitialStageSet,
			WaitForCpuCacheHit,
			WaitForDerivedDataCacheHit,
			WaitForCpuPartialHit,
			WaitForDerivedDataPartialHit,
			WaitForRestore,
			Completed,
		};

		size_t m_PreviousActiveIndex = EnvironmentAssetController::InvalidEntryIndex;
		size_t m_ExpectedActiveIndex = EnvironmentAssetController::InvalidEntryIndex;
		size_t m_ProbeEntryIndex = EnvironmentAssetController::InvalidEntryIndex;
		float m_ElapsedSeconds = 0.0f;
		uint64_t m_PreviousIBLGeneration = 0;
		uint64_t m_DerivedDataHitCountBaseline = 0;
		std::array<DerivedDataKey, static_cast<size_t>(IBLArtifactStage::Count)> m_IBLKeys{};
		std::array<ArtifactContentDigest, static_cast<size_t>(IBLArtifactStage::Count)>
			m_IBLArtifactDigests{};
		IBLQualityPreset m_OriginalQualityPreset = IBLQualityPreset::Medium;
		uint32_t m_OriginalSpecularSampleCount = 0;
		uint32_t m_CpuPartialSpecularSampleCount = 0;
		uint32_t m_DdcPartialSpecularSampleCount = 0;
		Phase m_Phase = Phase::WaitForInitialEnvironment;
		bool m_Passed = false;
		std::vector<std::string> m_Errors;
	};

	EnvironmentAssetLabSession::EnvironmentAssetLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, std::make_unique<RenderPipelineForwardPBR>())
	{
	}

	void EnvironmentAssetLabSession::OnEnter() noexcept
	{
		m_State = std::make_unique<State>();
		IBLBakeScheduler* scheduler = m_Services.m_Renderer->GetIBLBakeScheduler();
		// The acceptance sequence needs deterministic stage misses even when a
		// previous run populated the same sample-count variants. DDC entries are
		// recoverable derived data, so start this cache-focused Lab from a clean set.
		scheduler->ClearArtifactCache();
		GGLAB_UNUSED(scheduler->ClearDerivedDataStore());
		const auto& settings = m_Services.m_Renderer->GetEnvironmentLightingSystem()->GetSettings();
		m_State->m_OriginalQualityPreset = settings.m_QualityPreset;
		m_State->m_OriginalSpecularSampleCount =
			settings.m_BakeConfig.m_PrefilteredSpecularSampleCount;
		m_State->m_CpuPartialSpecularSampleCount = m_State->m_OriginalSpecularSampleCount < 4096
			? m_State->m_OriginalSpecularSampleCount + 1
			: 4095;
		m_State->m_DdcPartialSpecularSampleCount =
			m_State->m_CpuPartialSpecularSampleCount < 4096
			? m_State->m_CpuPartialSpecularSampleCount + 1
			: 4094;
	}

	void EnvironmentAssetLabSession::OnExit() noexcept
	{
		if (m_State)
		{
			EnvironmentLightingSystem* environment =
				m_Services.m_Renderer->GetEnvironmentLightingSystem();
			if (m_State->m_OriginalQualityPreset != IBLQualityPreset::Custom)
			{
				environment->SetQualityPreset(m_State->m_OriginalQualityPreset);
			}
			else
			{
				environment->SetPrefilteredSpecularSampleCount(
					m_State->m_OriginalSpecularSampleCount);
			}
		}
		if (m_Services.m_EnvironmentAssetController)
		{
			if (m_Services.m_AssetManager->IsAcceptingCommands())
			{
				m_Services.m_EnvironmentAssetController->Initialize("Assets/Textures/Skybox");
			}
			else
			{
				m_Services.m_EnvironmentAssetController->Reset();
			}
		}
		m_State.reset();
	}

	void EnvironmentAssetLabSession::Update(float deltaTime) noexcept
	{
		GetCamera().Update();
		if (!m_State || m_State->m_Phase == State::Phase::Completed)
		{
			return;
		}

		m_State->m_ElapsedSeconds += deltaTime;
		if (m_State->m_ElapsedSeconds > 120.0f)
		{
			Fail("Environment asset verification timed out.");
			return;
		}

		EnvironmentAssetController& controller = *m_Services.m_EnvironmentAssetController;
		const auto entries = controller.GetEntries();
		switch (m_State->m_Phase)
		{
		case State::Phase::WaitForInitialEnvironment:
		{
			if (controller.GetPendingEnvironmentIndex() !=
				EnvironmentAssetController::InvalidEntryIndex)
			{
				break;
			}
			const size_t activeIndex = controller.GetActiveEnvironmentIndex();
			if (entries.size() < 4 || activeIndex >= entries.size())
			{
				break;
			}

			const size_t first = (activeIndex + 1) % entries.size();
			const size_t second = (activeIndex + 2) % entries.size();
			const size_t third = (activeIndex + 3) % entries.size();
			if (!controller.SelectEnvironment(first) || !controller.SelectEnvironment(second) ||
				!controller.SelectEnvironment(third))
			{
				Fail("Rapid A-to-B-to-C selection was rejected.");
				return;
			}
			if (controller.GetActiveEnvironmentIndex() != activeIndex ||
				controller.GetPendingEnvironmentIndex() != third)
			{
				Fail("Rapid selection changed the committed source before C became ready.");
				return;
			}
			m_State->m_PreviousActiveIndex = activeIndex;
			m_State->m_ExpectedActiveIndex = third;
			m_State->m_Phase = State::Phase::WaitForRapidSelection;
			break;
		}

		case State::Phase::WaitForRapidSelection:
		{
			if (controller.GetPendingEnvironmentIndex() !=
				EnvironmentAssetController::InvalidEntryIndex)
			{
				if (controller.GetActiveEnvironmentIndex() != m_State->m_PreviousActiveIndex)
				{
					Fail("The active environment changed while rapid-selection C was pending.");
				}
				break;
			}
			if (controller.GetActiveEnvironmentIndex() != m_State->m_ExpectedActiveIndex)
			{
				Fail("Rapid selection did not commit the last candidate.");
				return;
			}

			const size_t target = (m_State->m_ExpectedActiveIndex + 1) % entries.size();
			const uint64_t serialBeforeImmediateFailure = controller.GetSelectionSerial();
			if (!controller.SelectEnvironment(target))
			{
				Fail("Immediate-failure setup candidate was rejected.");
				return;
			}
			if (controller.SelectEnvironmentFile(
				"Assets/Textures/Skybox/__gglab_missing_environment__.hdr",
				"Immediate Failure Probe"))
			{
				Fail("Missing environment unexpectedly produced a valid load request.");
				return;
			}
			const auto entriesAfterFailure = controller.GetEntries();
			if (controller.GetActiveEnvironmentIndex() != m_State->m_ExpectedActiveIndex ||
				controller.GetPendingEnvironmentIndex() !=
				EnvironmentAssetController::InvalidEntryIndex ||
				controller.GetSelectionSerial() <= serialBeforeImmediateFailure ||
				entriesAfterFailure.empty() ||
				entriesAfterFailure.back().m_State != EnvironmentAssetEntryState::Failed)
			{
				Fail("An immediate selection failure did not invalidate the older pending candidate.");
				return;
			}
			if (!controller.SelectEnvironment(target) ||
				controller.GetActiveEnvironmentIndex() != m_State->m_ExpectedActiveIndex)
			{
				Fail("Transactional switch did not preserve the active environment.");
				return;
			}
			m_State->m_PreviousActiveIndex = m_State->m_ExpectedActiveIndex;
			m_State->m_ExpectedActiveIndex = target;
			m_State->m_Phase = State::Phase::WaitForTransactionalSwitch;
			break;
		}

		case State::Phase::WaitForTransactionalSwitch:
		{
			if (controller.GetPendingEnvironmentIndex() !=
				EnvironmentAssetController::InvalidEntryIndex)
			{
				if (controller.GetActiveEnvironmentIndex() != m_State->m_PreviousActiveIndex)
				{
					Fail("The active environment changed before the replacement was ready.");
				}
				break;
			}
			if (controller.GetActiveEnvironmentIndex() != m_State->m_ExpectedActiveIndex)
			{
				Fail("The ready replacement was not committed transactionally.");
				return;
			}
			if (!controller.SelectEnvironmentFile(
				"Shaders/Passes/PassForwardPBR.hlsl", "Decode Failure Probe"))
			{
				Fail("Decode-failure probe was rejected before asynchronous loading.");
				return;
			}
			m_State->m_ProbeEntryIndex = controller.GetPendingEnvironmentIndex();
			m_State->m_Phase = State::Phase::WaitForDecodeFailure;
			break;
		}

		case State::Phase::WaitForDecodeFailure:
		{
			if (controller.GetActiveEnvironmentIndex() != m_State->m_ExpectedActiveIndex)
			{
				Fail("A decode failure replaced the active environment.");
				return;
			}
			if (controller.GetPendingEnvironmentIndex() !=
				EnvironmentAssetController::InvalidEntryIndex)
			{
				break;
			}
			if (m_State->m_ProbeEntryIndex >= entries.size() ||
				entries[m_State->m_ProbeEntryIndex].m_State != EnvironmentAssetEntryState::Failed)
			{
				Fail("The decode-failure candidate did not end in Failed state.");
				return;
			}
			if (!controller.SelectEnvironmentFile(
				"Assets/Textures/UVTest1K.png", "Invalid Shape Probe"))
			{
				Fail("Invalid-shape probe was rejected before loading.");
				return;
			}
			m_State->m_ProbeEntryIndex = controller.GetPendingEnvironmentIndex();
			m_State->m_Phase = State::Phase::WaitForInvalidShape;
			break;
		}

		case State::Phase::WaitForInvalidShape:
		{
			if (controller.GetActiveEnvironmentIndex() != m_State->m_ExpectedActiveIndex)
			{
				Fail("An invalid-shape candidate replaced the active environment.");
				return;
			}
			if (controller.GetPendingEnvironmentIndex() !=
				EnvironmentAssetController::InvalidEntryIndex)
			{
				break;
			}
			if (m_State->m_ProbeEntryIndex >= entries.size() ||
				entries[m_State->m_ProbeEntryIndex].m_State !=
				EnvironmentAssetEntryState::InvalidShape)
			{
				Fail("The non-2:1 candidate did not end in InvalidShape state.");
				return;
			}

			controller.Reset();
			const EnvironmentTextureSource& source =
				m_Services.m_Renderer->GetEnvironmentLightingSystem()->GetBakeSource();
			if (controller.GetActiveEnvironment() ||
				source.m_Type != EnvironmentTextureSourceType::Cubemap ||
				!IsReservedTextureId(source.m_Content.m_Id))
			{
				Fail("Reset did not synchronously commit the pinned fallback environment.");
				return;
			}
			m_State->m_Phase = State::Phase::ObserveFallback;
			break;
		}

		case State::Phase::ObserveFallback:
			controller.Initialize("Assets/Textures/Skybox");
			m_State->m_Phase = State::Phase::WaitForReselection;
			break;

		case State::Phase::WaitForReselection:
		{
			if (controller.GetPendingEnvironmentIndex() !=
				EnvironmentAssetController::InvalidEntryIndex)
			{
				break;
			}
			if (!controller.GetActiveEnvironment() ||
				m_Services.m_Renderer->GetEnvironmentLightingSystem()->GetBakeSource().m_Type !=
				EnvironmentTextureSourceType::Equirectangular)
			{
				Fail("Environment reselection did not replace the fallback.");
				return;
			}
			m_State->m_Phase = State::Phase::WaitForInitialStageSet;
			break;
		}

		case State::Phase::WaitForInitialStageSet:
		{
			IBLBakeScheduler& scheduler = *m_Services.m_Renderer->GetIBLBakeScheduler();
			const IBLBakeStatus& status = scheduler.GetStatus();
			if (status.m_Stage == IBLBakeStage::Failed)
			{
				Fail("The final environment IBL stage set failed to bake or load.");
				return;
			}
			if (status.m_Stage != IBLBakeStage::Ready ||
				status.m_ActiveGeneration != status.m_RequestedGeneration ||
				status.m_CacheWritePending)
			{
				break;
			}
			const auto cpuCache = scheduler.GetArtifactCacheStatistics();
			const auto ddc = scheduler.GetDerivedDataStoreStatistics();
			const bool validArtifacts = std::ranges::all_of(status.m_Artifacts,
				[](const IBLStageArtifactStatus& artifact) noexcept
				{
					return artifact.m_DerivedDataKey.IsValid() &&
						artifact.m_ContentDigest.IsValid();
				});
			if (!validArtifacts ||
				cpuCache.m_CachedEntryCount < static_cast<uint32_t>(IBLArtifactStage::Count) ||
				ddc.m_StoredEntryCount < static_cast<uint32_t>(IBLArtifactStage::Count))
			{
				Fail("The ready IBL stage set was not published to both CPU cache and local DDC.");
				return;
			}
			for (size_t index = 0; index < status.m_Artifacts.size(); ++index)
			{
				m_State->m_IBLKeys[index] = status.m_Artifacts[index].m_DerivedDataKey;
				m_State->m_IBLArtifactDigests[index] = status.m_Artifacts[index].m_ContentDigest;
			}
			m_State->m_PreviousIBLGeneration = status.m_ActiveGeneration;
			m_Services.m_Renderer->GetEnvironmentLightingSystem()->RequestRebake(false);
			m_State->m_Phase = State::Phase::WaitForCpuCacheHit;
			break;
		}

		case State::Phase::WaitForCpuCacheHit:
		{
			IBLBakeScheduler& scheduler = *m_Services.m_Renderer->GetIBLBakeScheduler();
			const IBLBakeStatus& status = scheduler.GetStatus();
			if (status.m_Stage == IBLBakeStage::Failed)
			{
				Fail("The IBL CPU cache reload failed.");
				return;
			}
			if (status.m_Stage != IBLBakeStage::Ready ||
				status.m_ActiveGeneration == m_State->m_PreviousIBLGeneration)
			{
				if (status.m_ActiveGeneration != m_State->m_PreviousIBLGeneration)
				{
					Fail(
						"The CPU cache reload replaced the active IBL stage set before publication.");
				}
				break;
			}
			bool exactCpuHit =
				status.m_ActiveGeneration == status.m_RequestedGeneration && status.m_CacheHit &&
				!status.m_PartialCacheHit &&
				status.m_CacheHitStageCount == static_cast<uint32_t>(IBLArtifactStage::Count) &&
				status.m_GpuBuildStageCount == 0;
			for (size_t index = 0; index < status.m_Artifacts.size(); ++index)
			{
				exactCpuHit &=
					status.m_Artifacts[index].m_Resolution == IBLArtifactResolution::CpuCache;
				exactCpuHit &=
					status.m_Artifacts[index].m_DerivedDataKey == m_State->m_IBLKeys[index];
				exactCpuHit &= status.m_Artifacts[index].m_ContentDigest ==
					m_State->m_IBLArtifactDigests[index];
			}
			if (!exactCpuHit)
			{
				Fail("The repeated IBL request did not reuse the exact CPU stage artifacts.");
				return;
			}

			m_State->m_PreviousIBLGeneration = status.m_ActiveGeneration;
			scheduler.ClearArtifactCache();
			if (scheduler.GetArtifactCacheStatistics().m_CachedEntryCount != 0)
			{
				Fail("Clearing the IBL CPU cache left cached stage entries behind.");
				return;
			}
			m_State->m_DerivedDataHitCountBaseline =
				scheduler.GetDerivedDataStoreStatistics().m_HitCount;
			m_Services.m_Renderer->GetEnvironmentLightingSystem()->RequestRebake(false);
			m_State->m_Phase = State::Phase::WaitForDerivedDataCacheHit;
			break;
		}

		case State::Phase::WaitForDerivedDataCacheHit:
		{
			IBLBakeScheduler& scheduler = *m_Services.m_Renderer->GetIBLBakeScheduler();
			const IBLBakeStatus& status = scheduler.GetStatus();
			if (status.m_Stage == IBLBakeStage::Failed)
			{
				Fail("The IBL local DDC reload failed.");
				return;
			}
			if (status.m_Stage != IBLBakeStage::Ready ||
				status.m_ActiveGeneration == m_State->m_PreviousIBLGeneration)
			{
				if (status.m_ActiveGeneration != m_State->m_PreviousIBLGeneration)
				{
					Fail(
						"The local DDC reload replaced the active IBL stage set before publication.");
				}
				break;
			}
			bool exactDdcHit =
				status.m_ActiveGeneration == status.m_RequestedGeneration && status.m_CacheHit &&
				!status.m_PartialCacheHit &&
				status.m_CacheHitStageCount == static_cast<uint32_t>(IBLArtifactStage::Count) &&
				status.m_GpuBuildStageCount == 0;
			for (size_t index = 0; index < status.m_Artifacts.size(); ++index)
			{
				exactDdcHit &=
					status.m_Artifacts[index].m_Resolution == IBLArtifactResolution::LocalDdc;
				exactDdcHit &=
					status.m_Artifacts[index].m_DerivedDataKey == m_State->m_IBLKeys[index];
				exactDdcHit &= status.m_Artifacts[index].m_ContentDigest ==
					m_State->m_IBLArtifactDigests[index];
			}
			if (!exactDdcHit || scheduler.GetDerivedDataStoreStatistics().m_HitCount <
				m_State->m_DerivedDataHitCountBaseline +
				static_cast<uint32_t>(IBLArtifactStage::Count))
			{
				Fail("The IBL request did not restore the exact stage artifacts from local DDC.");
				return;
			}

			m_State->m_PreviousIBLGeneration = status.m_ActiveGeneration;
			m_Services.m_Renderer->GetEnvironmentLightingSystem()
				->SetPrefilteredSpecularSampleCount(m_State->m_CpuPartialSpecularSampleCount);
			m_State->m_Phase = State::Phase::WaitForCpuPartialHit;
			break;
		}

		case State::Phase::WaitForCpuPartialHit:
		{
			IBLBakeScheduler& scheduler = *m_Services.m_Renderer->GetIBLBakeScheduler();
			const IBLBakeStatus& status = scheduler.GetStatus();
			if (status.m_Stage == IBLBakeStage::Failed)
			{
				Fail("The IBL CPU partial-hit bake failed.");
				return;
			}
			if (status.m_Stage != IBLBakeStage::Ready ||
				status.m_ActiveGeneration == m_State->m_PreviousIBLGeneration)
			{
				if (status.m_Stage != IBLBakeStage::Ready &&
					status.m_ActiveGeneration != m_State->m_PreviousIBLGeneration)
				{
					Fail(
						"A CPU partial hit replaced the active IBL before its complete stage set was ready.");
				}
				break;
			}
			if (status.m_CacheWritePending)
			{
				break;
			}

			const size_t specular = static_cast<size_t>(IBLArtifactStage::PrefilteredSpecular);
			bool partialHit = status.m_ActiveGeneration == status.m_RequestedGeneration &&
				status.m_PartialCacheHit && !status.m_CacheHit &&
				status.m_CacheHitStageCount == 3 && status.m_GpuBuildStageCount == 1;
			for (size_t index = 0; index < status.m_Artifacts.size(); ++index)
			{
				const auto& artifact = status.m_Artifacts[index];
				if (index == specular)
				{
					partialHit &= artifact.m_Resolution == IBLArtifactResolution::GpuBuild;
					partialHit &= artifact.m_DerivedDataKey != m_State->m_IBLKeys[index];
					partialHit &= artifact.m_ContentDigest.IsValid();
				}
				else
				{
					partialHit &= artifact.m_Resolution == IBLArtifactResolution::CpuCache;
					partialHit &= artifact.m_DerivedDataKey == m_State->m_IBLKeys[index];
					partialHit &= artifact.m_ContentDigest == m_State->m_IBLArtifactDigests[index];
				}
			}
			if (!partialHit)
			{
				Fail(
					"Changing only specular samples did not produce a 3-stage CPU hit plus 1-stage GPU build.");
				return;
			}

			m_State->m_PreviousIBLGeneration = status.m_ActiveGeneration;
			scheduler.ClearArtifactCache();
			m_State->m_DerivedDataHitCountBaseline =
				scheduler.GetDerivedDataStoreStatistics().m_HitCount;
			m_Services.m_Renderer->GetEnvironmentLightingSystem()
				->SetPrefilteredSpecularSampleCount(m_State->m_DdcPartialSpecularSampleCount);
			m_State->m_Phase = State::Phase::WaitForDerivedDataPartialHit;
			break;
		}

		case State::Phase::WaitForDerivedDataPartialHit:
		{
			IBLBakeScheduler& scheduler = *m_Services.m_Renderer->GetIBLBakeScheduler();
			const IBLBakeStatus& status = scheduler.GetStatus();
			if (status.m_Stage == IBLBakeStage::Failed)
			{
				Fail("The IBL local DDC partial-hit bake failed.");
				return;
			}
			if (status.m_Stage != IBLBakeStage::Ready ||
				status.m_ActiveGeneration == m_State->m_PreviousIBLGeneration)
			{
				if (status.m_Stage != IBLBakeStage::Ready &&
					status.m_ActiveGeneration != m_State->m_PreviousIBLGeneration)
				{
					Fail(
						"A DDC partial hit replaced the active IBL before its complete stage set was ready.");
				}
				break;
			}
			if (status.m_CacheWritePending)
			{
				break;
			}

			const size_t specular = static_cast<size_t>(IBLArtifactStage::PrefilteredSpecular);
			bool partialHit = status.m_ActiveGeneration == status.m_RequestedGeneration &&
				status.m_PartialCacheHit && !status.m_CacheHit &&
				status.m_CacheHitStageCount == 3 && status.m_GpuBuildStageCount == 1;
			for (size_t index = 0; index < status.m_Artifacts.size(); ++index)
			{
				const auto& artifact = status.m_Artifacts[index];
				if (index == specular)
				{
					partialHit &= artifact.m_Resolution == IBLArtifactResolution::GpuBuild;
					partialHit &= artifact.m_ContentDigest.IsValid();
				}
				else
				{
					partialHit &= artifact.m_Resolution == IBLArtifactResolution::LocalDdc;
					partialHit &= artifact.m_DerivedDataKey == m_State->m_IBLKeys[index];
					partialHit &= artifact.m_ContentDigest == m_State->m_IBLArtifactDigests[index];
				}
			}
			partialHit &= scheduler.GetDerivedDataStoreStatistics().m_HitCount >=
				m_State->m_DerivedDataHitCountBaseline + 3;
			if (!partialHit)
			{
				Fail(
					"Changing only specular samples did not produce a 3-stage DDC hit plus 1-stage GPU build.");
				return;
			}

			m_State->m_PreviousIBLGeneration = status.m_ActiveGeneration;
			EnvironmentLightingSystem* environment =
				m_Services.m_Renderer->GetEnvironmentLightingSystem();
			if (m_State->m_OriginalQualityPreset != IBLQualityPreset::Custom)
			{
				environment->SetQualityPreset(m_State->m_OriginalQualityPreset);
			}
			else
			{
				environment->SetPrefilteredSpecularSampleCount(
					m_State->m_OriginalSpecularSampleCount);
			}
			m_State->m_Phase = State::Phase::WaitForRestore;
			break;
		}

		case State::Phase::WaitForRestore:
		{
			const IBLBakeStatus& status = m_Services.m_Renderer->GetIBLBakeScheduler()->GetStatus();
			if (status.m_Stage == IBLBakeStage::Failed)
			{
				Fail("Restoring the original IBL configuration failed.");
				return;
			}
			if (status.m_Stage != IBLBakeStage::Ready ||
				status.m_ActiveGeneration == m_State->m_PreviousIBLGeneration)
			{
				if (status.m_ActiveGeneration != m_State->m_PreviousIBLGeneration)
				{
					Fail(
						"Restoring the original IBL replaced the active set before atomic publication.");
				}
				break;
			}
			bool restored = status.m_CacheHit && status.m_GpuBuildStageCount == 0;
			for (size_t index = 0; index < status.m_Artifacts.size(); ++index)
			{
				restored &= status.m_Artifacts[index].m_DerivedDataKey == m_State->m_IBLKeys[index];
				restored &= status.m_Artifacts[index].m_ContentDigest ==
					m_State->m_IBLArtifactDigests[index];
			}
			if (!restored)
			{
				Fail("The original IBL stage set was not restored exactly.");
				return;
			}
			Complete();
			break;
		}

		case State::Phase::Completed:
			break;
		}
	}

	void EnvironmentAssetLabSession::BuildDiagnostics(
		LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		diagnostics.m_Title = "Environment Asset Verification";
		if (!m_State)
		{
			return;
		}
		diagnostics.m_Metrics = {
			{.m_Name = "Elapsed", .m_Value = std::format("{:.2f} s", m_State->m_ElapsedSeconds)},
			{.m_Name = "Selection phase",
				.m_Value = std::to_string(std::to_underlying(m_State->m_Phase))},
		};
		diagnostics.m_Checks.push_back({
			.m_Name = "Transactional environment selection",
			.m_Status = m_State->m_Phase != State::Phase::Completed
							? LabDiagnosticCheckStatus::Pending
						: m_State->m_Passed ? LabDiagnosticCheckStatus::Passed
											: LabDiagnosticCheckStatus::Failed,
			.m_Detail = m_State->m_Phase != State::Phase::Completed ? "Verification is running."
						: m_State->m_Passed
							? "Environment selection and IBL cache/DDC invariants passed."
							: std::format("{} invariant errors.", m_State->m_Errors.size()),
			});
		for (const std::string& error : m_State->m_Errors)
		{
			diagnostics.m_Checks.push_back({
				.m_Name = "Invariant",
				.m_Status = LabDiagnosticCheckStatus::Failed,
				.m_Detail = error,
				});
		}
	}

	void EnvironmentAssetLabSession::Fail(std::string error) noexcept
	{
		if (!m_State || m_State->m_Phase == State::Phase::Completed)
		{
			return;
		}
		m_State->m_Errors.push_back(std::move(error));
		m_State->m_Passed = false;
		m_State->m_Phase = State::Phase::Completed;
		GGLAB_LOG_ERROR("ENVIRONMENT ASSET ACCEPTANCE FAIL: {}", m_State->m_Errors.back());
	}

	void EnvironmentAssetLabSession::Complete() noexcept
	{
		GGLAB_ASSERT(m_State);
		m_State->m_Passed = true;
		m_State->m_Phase = State::Phase::Completed;
		GGLAB_LOG_INFO(
			"ENVIRONMENT ASSET ACCEPTANCE PASS: transactional environment selection, atomic IBL stage publication, full and partial CPU cache reuse, and full and partial local DDC restoration invariants passed in {:.2f} s.",
			m_State->m_ElapsedSeconds);
	}

	LabId EnvironmentAssetLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.environment_assets");
	}

	LabDescriptor EnvironmentAssetLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Environment Asset Lab",
			.m_Category = "Systems",
			.m_Description =
				"Validates transactional HDR selection plus atomic full/partial IBL CPU cache and local DDC restoration.",
			.m_Kind = LabKind::Pipeline,
			.m_SchemaVersion = 3,
		};
	}

	std::unique_ptr<LabSessionBase> EnvironmentAssetLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<EnvironmentAssetLabSession>(createInfo);
	}
}
