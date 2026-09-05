#pragma once

#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

#include <cstdint>

namespace gglab
{
	struct ShadowTextureDiagnostics
	{
		RHIExtent3D m_Extent{};
		RHIFormat m_Format = RHIFormat::Unknown;
		bool m_Available = false;
	};

	struct ShadowDiagnosticsSnapshot
	{
		ShadowTextureDiagnostics m_DirectionalShadowMap{};
		ShadowTextureDiagnostics m_DirectionalShadowMapPreviewSource{};
		uint32_t m_ShadowMapSize = 0;
		uint32_t m_ShadowMapPreviewSize = 0;
		bool m_Available = false;
	};

	template <> struct SnapshotTraits<ShadowDiagnosticsSnapshot>
	{
		static constexpr SnapshotId Id =
			MakeSnapshotId("Diagnostics.ShadowDiagnosticsSnapshot");
	};
}
