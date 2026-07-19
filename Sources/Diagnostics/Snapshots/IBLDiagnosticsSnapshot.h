#pragma once
#include "Diagnostics/SnapshotCommon.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHITypes.h"
#include "Graphics/IBLBakeTypes.h"

#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace gglab
{
	enum class IBLBakeState : uint8_t
	{
		Unavailable,
		Dirty,
		Ready,
	};

	struct IBLTextureDiagnostics
	{
		IBLBakeState m_BakeState = IBLBakeState::Unavailable;
		uint64_t m_Width = 0;
		uint32_t m_Height = 0;
		uint16_t m_ArraySize = 0;
		uint16_t m_MipLevels = 0;
		RHIFormat m_Format = RHIFormat::Unknown;
		RHIDescriptorHandle m_SrvDescriptor{};
		uint32_t m_ShaderVisibleSrvIndex = 0;
	};

	struct IBLPreviewDiagnostics
	{
		IBLTextureDiagnostics m_Texture;
		uint32_t m_Layout = 0;
		uint32_t m_SelectedMip = 0;
		uint64_t m_UpdateCount = 0;
		bool m_Dirty = true;
		bool m_Requested = false;
	};

	enum class IBLEnvironmentEntryState : uint8_t
	{
		Unrequested,
		Loading,
		Ready,
		Failed,
		InvalidShape,
	};

	struct IBLEnvironmentEntryDiagnostics
	{
		size_t m_Index = 0;
		std::filesystem::path m_Path;
		std::string m_DisplayName;
		uint64_t m_LastSelectionSerial = 0;
		IBLEnvironmentEntryState m_State = IBLEnvironmentEntryState::Unrequested;
		bool m_Active = false;
	};

	struct IBLArtifactCacheDiagnostics
	{
		uint64_t m_BudgetBytes = 0;
		uint64_t m_CachedBytes = 0;
		uint64_t m_ExternallyRetainedBytes = 0;
		uint64_t m_TotalLiveBytes = 0;
		uint32_t m_CachedEntryCount = 0;
		uint64_t m_HitCount = 0;
		uint64_t m_MissCount = 0;
		uint64_t m_AdmissionCount = 0;
		uint64_t m_AdmissionRejectedCount = 0;
		uint64_t m_EvictionCount = 0;
		uint64_t m_EvictedBytes = 0;
	};

	struct IBLDerivedDataStoreDiagnostics
	{
		uint64_t m_StoredBytes = 0;
		uint64_t m_StoredEntryCount = 0;
		uint64_t m_HitCount = 0;
		uint64_t m_MissCount = 0;
		uint64_t m_CorruptionCount = 0;
		uint64_t m_ReadBytes = 0;
		uint64_t m_WriteCount = 0;
		uint64_t m_WriteFailureCount = 0;
		uint64_t m_WrittenBytes = 0;
	};

	struct IBLDiagnosticsSnapshot
	{
		std::vector<IBLEnvironmentEntryDiagnostics> m_Environments;
		size_t m_ActiveEnvironmentIndex = std::numeric_limits<size_t>::max();

		float m_Intensity = 1.0f;
		float m_RotationRadians = 0.0f;
		IBLQualityPreset m_QualityPreset = IBLQualityPreset::Medium;
		IBLBakeConfig m_BakeConfig{};
		uint32_t m_PrefilteredSpecularSampleCount = 0;
		float m_PrefilteredSpecularMaxSampleLuminance = 0.0f;
		bool m_SkyboxEnabled = true;
		IBLBakeStatus m_BakeStatus{};
		IBLArtifactCacheDiagnostics m_ArtifactCache;
		IBLDerivedDataStoreDiagnostics m_DerivedDataStore;

		IBLTextureDiagnostics m_Environment;
		IBLTextureDiagnostics m_Irradiance;
		IBLTextureDiagnostics m_PrefilteredSpecular;
		IBLTextureDiagnostics m_BrdfLut;

		IBLPreviewDiagnostics m_EnvironmentPreview;
		IBLPreviewDiagnostics m_IrradiancePreview;
		IBLPreviewDiagnostics m_PrefilteredSpecularPreview;
	};

	template<>
	struct SnapshotTraits<IBLDiagnosticsSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.IBLDiagnosticsSnapshot");
	};
}
