#pragma once

#include "GGLabRuntime/Graphics/Pipeline/GIFieldReadiness.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace gglab
{
	// Persisted Lab enum values: bit 0 selects rapid reset, bit 1 progressive lighting,
	// and bit 2 progressive directions. All former switch combinations remain available.
	enum class GIFieldExperimentRecipe : int32_t
	{
		Default = 0,
		RapidReset = 1,
		ProgressiveLighting = 2,
		RapidResetProgressiveLighting = 3,
		ProgressiveSampling = 4,
		RapidResetProgressiveSampling = 5,
		ProgressiveLightingAndSampling = 6,
		RapidResetProgressiveLightingAndSampling = 7,
	};

	inline constexpr std::array<std::string_view, 8> GIFieldExperimentRecipeNames
	{
		"Default: progressive reset, rapid lighting",
		"Rapid reset and lighting (control)",
		"Progressive reset and lighting (experimental)",
		"Rapid reset, progressive lighting (experimental)",
		"Default refresh, progressive directions (experimental)",
		"Rapid refresh, progressive directions (experimental)",
		"Progressive refresh and directions (experimental)",
		"Rapid reset, progressive lighting and directions (experimental)",
	};

	struct GIFieldExperimentSettings
	{
		GIFieldRefreshIntegration m_HardReset;
		GIFieldRefreshIntegration m_Lighting;
		bool m_ProgressiveSampling;
	};

	[[nodiscard]] constexpr GIFieldExperimentSettings ResolveGIFieldExperimentRecipe(
		GIFieldExperimentRecipe recipe) noexcept
	{
		const auto value = static_cast<uint32_t>(recipe);
		return {
			.m_HardReset = (value & 1u) != 0 ?
				GIFieldRefreshIntegration::Rapid :
				GIFieldRefreshIntegration::Progressive,
			.m_Lighting = (value & 2u) != 0 ?
				GIFieldRefreshIntegration::Progressive :
				GIFieldRefreshIntegration::Rapid,
			.m_ProgressiveSampling = (value & 4u) != 0,
		};
	}
}
