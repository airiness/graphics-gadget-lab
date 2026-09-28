#pragma once

#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"
#include "GGLabRuntime/Graphics/ScreenSpace/ScreenSpaceTypes.h"

#include <cstdint>

namespace gglab
{
	struct SceneDepthDiagnosticsSnapshot
	{
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHIFormat m_ResourceFormat = RHIFormat::Unknown;
		RHIFormat m_DsvFormat = RHIFormat::Unknown;
		RHIFormat m_SrvFormat = RHIFormat::Unknown;
		float m_ClearDepth = 0.0f;
		DepthConvention m_Convention = DepthConvention::Standard;
		bool m_HasTypedClear = false;
		bool m_Available = false;
	};

	template <> struct SnapshotTraits<SceneDepthDiagnosticsSnapshot>
	{
		static constexpr SnapshotId Id =
			MakeSnapshotId("Diagnostics.SceneDepthDiagnosticsSnapshot");
	};
}
