#pragma once
#include "Diagnostics/SnapshotCommon.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHITypes.h"

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

	struct IBLEnvironmentEntryDiagnostics
	{
		size_t m_Index = 0;
		std::filesystem::path m_Path;
		std::string m_DisplayName;
		bool m_Active = false;
		bool m_TextureReady = false;
		bool m_LoadAttempted = false;
	};

	struct IBLDiagnosticsSnapshot
	{
		std::vector<IBLEnvironmentEntryDiagnostics> m_Environments;
		size_t m_ActiveEnvironmentIndex = std::numeric_limits<size_t>::max();

		float m_Intensity = 1.0f;
		float m_RotationRadians = 0.0f;
		uint32_t m_PrefilteredSpecularSampleCount = 0;
		bool m_SkyboxEnabled = true;

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
