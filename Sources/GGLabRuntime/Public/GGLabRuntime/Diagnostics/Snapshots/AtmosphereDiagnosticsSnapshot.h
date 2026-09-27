#pragma once

#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"

#include <string>
#include <optional>
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
		uint64_t m_ActiveWorldLightingGeneration = 0;
		uint64_t m_RequestedWorldLightingGeneration = 0;
		bool m_PhysicalSkyActive = false;
		bool m_PhysicalSkyRequested = false;
		std::optional<ResolvedWorldSun> m_ActiveSun;
		float m_ReferenceObserverAltitudeMeters = 1.0f;
		float m_ObserverMinAltitudeMeters = 0.0f;
		float m_ObserverMaxAltitudeMeters = 100.0f;
		double m_PublicationMilliseconds = 0.0;
		uint32_t m_RetiringTextureCount = 0;
	};

	template <> struct SnapshotTraits<AtmosphereDiagnosticsSnapshot>
	{
		static constexpr SnapshotId Id =
			MakeSnapshotId("Diagnostics.AtmosphereDiagnosticsSnapshot");
	};
}
