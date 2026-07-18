#include "Core/Precompiled.h"
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

	namespace
	{
		IBLTextureDiagnostics BuildTextureDiagnostics(
			const RenderResourceRegistry& registry,
			RenderResourceRegistry::TextureIndex index) noexcept
		{
			IBLTextureDiagnostics diagnostics{};
			const auto* desc = registry.GetTextureDesc(index);
			if (!desc)
			{
				return diagnostics;
			}

			diagnostics.m_BakeState = registry.IsDirty(index) ? IBLBakeState::Dirty : IBLBakeState::Ready;
			diagnostics.m_Width = desc->m_Extent.m_Width;
			diagnostics.m_Height = desc->m_Extent.m_Height;
			diagnostics.m_ArraySize = desc->m_ArraySize;
			diagnostics.m_MipLevels = desc->m_MipLevels;
			diagnostics.m_Format = desc->m_Format;
			diagnostics.m_SrvDescriptor = registry.GetSrvDescriptor(index);
			diagnostics.m_ShaderVisibleSrvIndex = diagnostics.m_SrvDescriptor.m_Index;
			return diagnostics;
		}

		IBLPreviewDiagnostics BuildPreviewDiagnostics(
			const RenderResourceRegistry& registry,
			RenderResourceRegistry::TextureIndex textureIndex,
			RenderResourceRegistry::IBLPreviewType previewType,
			RenderResourceRegistry::IBLPreviewLayout layout,
			uint32_t selectedMip) noexcept
		{
			return {
				.m_Texture = BuildTextureDiagnostics(registry, textureIndex),
				.m_Layout = static_cast<uint32_t>(layout),
				.m_SelectedMip = selectedMip,
				.m_UpdateCount = registry.GetIBLPreviewUpdateCount(previewType),
				.m_Dirty = registry.IsIBLPreviewDirty(previewType),
				.m_Requested = registry.IsIBLPreviewRequested(previewType),
			};
		}
	}

	IBLDiagnosticsSnapshot BuildIBLDiagnosticsSnapshot(
		const Renderer& renderer,
		const EnvironmentAssetController* environmentAssets) noexcept
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
		snapshot.m_PrefilteredSpecularSampleCount = settings.m_BakeConfig.m_PrefilteredSpecularSampleCount;
		snapshot.m_PrefilteredSpecularMaxSampleLuminance =
			settings.m_BakeConfig.m_PrefilteredSpecularMaxSampleLuminance;
		snapshot.m_SkyboxEnabled = settings.m_EnableSkybox;
		snapshot.m_BakeStatus = bakeScheduler->GetStatus();

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
					.m_TextureReady = environmentAssets->IsEntryTextureReady(index),
				});
			}
		}

		using TextureIndex = RenderResourceRegistry::TextureIndex;
		using PreviewType = RenderResourceRegistry::IBLPreviewType;
		snapshot.m_Environment = BuildTextureDiagnostics(*registry, TextureIndex::IBL_EnvironmentCubemap);
		snapshot.m_Irradiance = BuildTextureDiagnostics(*registry, TextureIndex::IBL_IrradianceCubemap);
		snapshot.m_PrefilteredSpecular = BuildTextureDiagnostics(
			*registry,
			TextureIndex::IBL_PrefilteredSpecularCubemap);
		snapshot.m_BrdfLut = BuildTextureDiagnostics(*registry, TextureIndex::IBL_BrdfLut);

		snapshot.m_EnvironmentPreview = BuildPreviewDiagnostics(
			*registry,
			TextureIndex::Preview_IBL_EnvironmentCubemap,
			PreviewType::Environment,
			registry->GetIBLEnvironmentPreviewLayout(),
			registry->GetIBLEnvironmentPreviewMip());
		snapshot.m_IrradiancePreview = BuildPreviewDiagnostics(
			*registry,
			TextureIndex::Preview_IBL_IrradianceCubemap,
			PreviewType::Irradiance,
			registry->GetIBLIrradiancePreviewLayout(),
			0);
		snapshot.m_PrefilteredSpecularPreview = BuildPreviewDiagnostics(
			*registry,
			TextureIndex::Preview_IBL_PrefilteredSpecularCubemap,
			PreviewType::PrefilteredSpecular,
			registry->GetIBLPrefilteredSpecularPreviewLayout(),
			registry->GetIBLPrefilteredSpecularPreviewMip());
		return snapshot;
	}
}
