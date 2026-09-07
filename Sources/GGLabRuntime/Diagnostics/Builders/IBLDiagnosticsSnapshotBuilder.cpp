#include "Diagnostics/Builders/IBLDiagnosticsSnapshotBuilder.h"
#include "Diagnostics/Snapshots/IBLDiagnosticsSnapshot.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/EnvironmentAssetController.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/Renderer.h"
#include "Graphics/Resource/RenderResourceRegistry.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] IBLEnvironmentEntryState ToDiagnosticState(
			EnvironmentAssetEntryState state) noexcept
		{
			switch (state)
			{
			case EnvironmentAssetEntryState::Unrequested:
				return IBLEnvironmentEntryState::Unrequested;
			case EnvironmentAssetEntryState::Loading:
				return IBLEnvironmentEntryState::Loading;
			case EnvironmentAssetEntryState::Ready:
				return IBLEnvironmentEntryState::Ready;
			case EnvironmentAssetEntryState::Failed:
				return IBLEnvironmentEntryState::Failed;
			case EnvironmentAssetEntryState::InvalidShape:
				return IBLEnvironmentEntryState::InvalidShape;
			}
			return IBLEnvironmentEntryState::Unrequested;
		}
	}

	IBLDiagnosticsSnapshot BuildIBLDiagnosticsSnapshot(
		const Renderer& renderer, const EnvironmentAssetController* environmentAssets) noexcept
	{
		IBLDiagnosticsSnapshot snapshot{};
		const auto* environmentSystem = renderer.GetEnvironmentLightingSystem();
		const auto* registry = renderer.GetRenderResourceRegistry();
		const auto* bakeScheduler = renderer.GetIBLBakeScheduler();
		if (!environmentSystem || !registry || !bakeScheduler)
		{
			return snapshot;
		}

		const auto& settings = environmentSystem->GetSettings();
		snapshot.m_Intensity = settings.m_Intensity;
		snapshot.m_RotationRadians = settings.m_RotationRadians;
		snapshot.m_QualityPreset = settings.m_QualityPreset;
		snapshot.m_BakeConfig = settings.m_BakeConfig;
		snapshot.m_PrefilteredSpecularSampleCount =
			settings.m_BakeConfig.m_PrefilteredSpecularSampleCount;
		snapshot.m_PrefilteredSpecularMaxSampleLuminance =
			settings.m_BakeConfig.m_PrefilteredSpecularMaxSampleLuminance;
		snapshot.m_SkyboxEnabled = settings.m_EnableSkybox;
		snapshot.m_BakeStatus = bakeScheduler->GetStatus();
		const auto artifactCache = bakeScheduler->GetArtifactCacheStatistics();
		snapshot.m_ArtifactCache = {
			.m_BudgetBytes = artifactCache.m_BudgetBytes,
			.m_CachedBytes = artifactCache.m_CachedBytes,
			.m_ExternallyRetainedBytes = artifactCache.m_ExternallyRetainedBytes,
			.m_TotalLiveBytes = artifactCache.m_TotalLiveBytes,
			.m_CachedEntryCount = artifactCache.m_CachedEntryCount,
			.m_HitCount = artifactCache.m_HitCount,
			.m_MissCount = artifactCache.m_MissCount,
			.m_AdmissionCount = artifactCache.m_AdmissionCount,
			.m_AdmissionRejectedCount = artifactCache.m_AdmissionRejectedCount,
			.m_EvictionCount = artifactCache.m_EvictionCount,
			.m_EvictedBytes = artifactCache.m_EvictedBytes,
		};
		const auto derivedDataStore = bakeScheduler->GetDerivedDataStoreStatistics();
		snapshot.m_DerivedDataStore = {
			.m_StoredBytes = derivedDataStore.m_StoredBytes,
			.m_StoredEntryCount = derivedDataStore.m_StoredEntryCount,
			.m_HitCount = derivedDataStore.m_HitCount,
			.m_MissCount = derivedDataStore.m_MissCount,
			.m_CorruptionCount = derivedDataStore.m_CorruptionCount,
			.m_ReadBytes = derivedDataStore.m_ReadBytes,
			.m_WriteCount = derivedDataStore.m_WriteCount,
			.m_WriteFailureCount = derivedDataStore.m_WriteFailureCount,
			.m_WrittenBytes = derivedDataStore.m_WrittenBytes,
			.m_CatalogLastReconciledAtUnixMilliseconds =
				derivedDataStore.m_CatalogLastReconciledAtUnixMilliseconds,
			.m_CatalogReconciliationCount = derivedDataStore.m_CatalogReconciliationCount,
			.m_CatalogReconciliationFailureCount =
				derivedDataStore.m_CatalogReconciliationFailureCount,
			.m_IsCatalogApproximate = derivedDataStore.m_IsCatalogApproximate,
		};

		if (environmentAssets)
		{
			const auto environments = environmentAssets->GetEntries();
			const auto* activeEnvironment = environmentAssets->GetActiveEnvironment();
			snapshot.m_Environments.reserve(environments.size());
			for (size_t index = 0; index < environments.size(); ++index)
			{
				const auto& environment = environments[index];
				const bool active = &environment == activeEnvironment;
				if (active)
				{
					snapshot.m_ActiveEnvironmentIndex = index;
				}
				snapshot.m_Environments.push_back({
					.m_Index = index,
					.m_Path = environment.m_Path,
					.m_DisplayName = environment.m_DisplayName,
					.m_LastSelectionSerial = environment.m_LastSelectionSerial,
					.m_State = ToDiagnosticState(environment.m_State),
					.m_Active = active,
					});
			}
		}

		const auto resources = registry->GetIBLPreviewResourcesDiagnostics();
		snapshot.m_Environment = resources.m_Environment;
		snapshot.m_Irradiance = resources.m_Irradiance;
		snapshot.m_PrefilteredSpecular = resources.m_PrefilteredSpecular;
		snapshot.m_BrdfLut = resources.m_BrdfLut;
		snapshot.m_EnvironmentPreview = resources.m_EnvironmentPreview;
		snapshot.m_IrradiancePreview = resources.m_IrradiancePreview;
		snapshot.m_PrefilteredSpecularPreview = resources.m_PrefilteredSpecularPreview;
		return snapshot;
	}
}
