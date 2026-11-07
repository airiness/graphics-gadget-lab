#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIFieldProfile.h"

#include <cstdint>

namespace gglab
{
	inline constexpr char CoastalSceneGIFieldProfileId[] = "gglab.gi.field.coastal.diffuse";
	inline constexpr uint32_t CoastalSceneGIFieldProfileRevision = 1;

	// Authored above-water coverage. This labels the base preset, not a supported-
	// profile certificate; the Runtime receipt also records every effective override.
	inline const GIFieldVolumeDescriptor CoastalSceneGIFieldVolume
	{
		.m_Counts = { 33, 10, 39 },
		.m_Minimum = { -17.25f, -1.5f, -23.25f },
		.m_Spacing = 1,
		.m_Profile = { CoastalSceneGIFieldProfileId, CoastalSceneGIFieldProfileRevision },
	};
}
