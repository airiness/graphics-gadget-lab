#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIDescriptor.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

#include <cstdint>

namespace gglab
{
	// Allocation and dirty state are not GPU completion guarantees.
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

	// Copied active resources; staging bake allocations are never exposed.
	// Layout and mip describe requested selection. Update counts track recording,
	// not fence completion or the generation of the current allocation.
	struct IBLPreviewResourcesDiagnostics
	{
		IBLTextureDiagnostics m_Environment;
		IBLTextureDiagnostics m_Irradiance;
		IBLTextureDiagnostics m_PrefilteredSpecular;
		IBLTextureDiagnostics m_BrdfLut;
		IBLPreviewDiagnostics m_EnvironmentPreview;
		IBLPreviewDiagnostics m_IrradiancePreview;
		IBLPreviewDiagnostics m_PrefilteredSpecularPreview;
	};
}
