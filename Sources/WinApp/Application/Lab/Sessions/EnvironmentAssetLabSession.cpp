#include "Application/Lab/Sessions/EnvironmentAssetLabSession.h"
#include "AppRuntimeLog.h"
#include "GGLabFoundation/Base/TypeUtils.h"
#include "GGLabRuntime/Diagnostics/Snapshots/LabSnapshot.h"
#include "GGLabRuntime/Graphics/EnvironmentAssetController.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/IBLCacheControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentTextureSource.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPlus.h"

namespace gglab
{
	struct EnvironmentAssetLabSession::State
	{
		enum class Phase : uint8_t
		{
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
		Phase m_Phase = Phase::ObserveFallback;
		bool m_Passed = false;
		std::vector<std::string> m_Errors;
	};

	EnvironmentAssetLabSession::EnvironmentAssetLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, CreateRenderPipelineForwardPlus())
	{
	}

	void EnvironmentAssetLabSession::OnEnter() noexcept
	{
		m_State = std::make_unique<State>();
		IBLCacheControlBase* cacheControl = m_Services.m_IBLCacheControl;
		GGLAB_ASSERT_NOT_NULL(cacheControl);
		// The acceptance sequence needs deterministic stage misses even when a
		// previous run populated the same sample-count variants. DDC entries are
		// recoverable derived data, so start this cache-focused Lab from a clean set.
		cacheControl->ClearArtifactCache();
		GGLAB_UNUSED(cacheControl->ClearDerivedDataStore());
		const auto& settings = m_Services.m_EnvironmentLighting->GetEnvironmentLightingSettings();
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
		// Committing the fallback and reselecting forces a fresh IBL stage set after
		// the caches were cleared. Selection transactions themselves are covered by
		// the environment-selection suite.
		m_Services.m_EnvironmentAssetController->Reset();
	}

	void EnvironmentAssetLabSession::OnExit() noexcept
	{
		if (m_State)
		{
			EnvironmentLightingControlBase* environment =
				m_Services.m_EnvironmentLightingControl;
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
		EnvironmentAssetController* environmentController =
			m_Services.m_EnvironmentAssetController;
		GGLAB_ASSERT_NOT_NULL(environmentController);
		if (m_Services.m_AssetManager->IsAcceptingCommands())
		{
			environmentController->Initialize("Assets/Textures/Skybox");
		}
		else
		{
			environmentController->Reset();
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
		switch (m_State->m_Phase)
		{
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
				m_Services.m_RenderServices.m_Environment->GetCommittedEnvironmentSource().m_Type !=
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
			RenderEnvironmentAccess& scheduler = *m_Services.m_RenderServices.m_Environment;
			const IBLBakeStatus& status = scheduler.GetBakingStatus();
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
			m_Services.m_EnvironmentLightingControl->RequestRebake(false);
			m_State->m_Phase = State::Phase::WaitForCpuCacheHit;
			break;
		}

		case State::Phase::WaitForCpuCacheHit:
		{
			RenderEnvironmentAccess& scheduler = *m_Services.m_RenderServices.m_Environment;
			const IBLBakeStatus& status = scheduler.GetBakingStatus();
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
			m_Services.m_IBLCacheControl->ClearArtifactCache();
			if (scheduler.GetArtifactCacheStatistics().m_CachedEntryCount != 0)
			{
				Fail("Clearing the IBL CPU cache left cached stage entries behind.");
				return;
			}
			m_State->m_DerivedDataHitCountBaseline =
				scheduler.GetDerivedDataStoreStatistics().m_HitCount;
			m_Services.m_EnvironmentLightingControl->RequestRebake(false);
			m_State->m_Phase = State::Phase::WaitForDerivedDataCacheHit;
			break;
		}

		case State::Phase::WaitForDerivedDataCacheHit:
		{
			RenderEnvironmentAccess& scheduler = *m_Services.m_RenderServices.m_Environment;
			const IBLBakeStatus& status = scheduler.GetBakingStatus();
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
			m_Services.m_EnvironmentLightingControl
				->SetPrefilteredSpecularSampleCount(m_State->m_CpuPartialSpecularSampleCount);
			m_State->m_Phase = State::Phase::WaitForCpuPartialHit;
			break;
		}

		case State::Phase::WaitForCpuPartialHit:
		{
			RenderEnvironmentAccess& scheduler = *m_Services.m_RenderServices.m_Environment;
			const IBLBakeStatus& status = scheduler.GetBakingStatus();
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
			m_Services.m_IBLCacheControl->ClearArtifactCache();
			m_State->m_DerivedDataHitCountBaseline =
				scheduler.GetDerivedDataStoreStatistics().m_HitCount;
			m_Services.m_EnvironmentLightingControl
				->SetPrefilteredSpecularSampleCount(m_State->m_DdcPartialSpecularSampleCount);
			m_State->m_Phase = State::Phase::WaitForDerivedDataPartialHit;
			break;
		}

		case State::Phase::WaitForDerivedDataPartialHit:
		{
			RenderEnvironmentAccess& scheduler = *m_Services.m_RenderServices.m_Environment;
			const IBLBakeStatus& status = scheduler.GetBakingStatus();
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
			EnvironmentLightingControlBase* environment =
				m_Services.m_EnvironmentLightingControl;
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
			const IBLBakeStatus& status = m_Services.m_RenderServices.m_Environment->GetBakingStatus();
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
				.m_Value = std::to_string(utils::ToUnderlying(m_State->m_Phase))},
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
				"Validates atomic full/partial IBL CPU cache and local DDC restoration of a reselected environment.",
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
