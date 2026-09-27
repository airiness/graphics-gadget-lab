#pragma once

#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"

#include <string>
#include <vector>

namespace gglab
{
	struct AtmosphereGpuPassDiagnostics
	{
		std::string m_Name;
		double m_Milliseconds = 0.0;
	};

	struct AtmosphereDiagnosticsSnapshot
	{
		AtmosphereDiagnostics m_State{};
		std::vector<AtmosphereGpuPassDiagnostics> m_GpuPasses;
		uint64_t m_GpuFrameIndex = 0;
		bool m_GpuTimingAvailable = false;
	};

	template <> struct SnapshotTraits<AtmosphereDiagnosticsSnapshot>
	{
		static constexpr SnapshotId Id =
			MakeSnapshotId("Diagnostics.AtmosphereDiagnosticsSnapshot");
	};
}
