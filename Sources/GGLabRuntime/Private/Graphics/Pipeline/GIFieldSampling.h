#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace gglab
{
	// Matches Lighting/GIFieldSampling.hlsli. Irradiance is scene-referred, without
	// pre-exposure; the atlas stores M and the query returns pi * M.
	struct GIFieldSamplingParameters
	{
		std::array<uint32_t, 3> m_Counts{};
		// Zero width disables sampling and descriptor resolution.
		uint32_t m_IrradianceInteriorWidth = 0;
		std::array<float, 3> m_Minimum{};
		float m_Spacing = 0;
		uint32_t m_Irradiance = UINT32_MAX;
		uint32_t m_Distance = UINT32_MAX;
		uint32_t m_Classification = UINT32_MAX;
		float m_ViewBias = 0.3f;
	};
	static_assert(sizeof(GIFieldSamplingParameters) == 48 &&
		offsetof(GIFieldSamplingParameters, m_Minimum) == 16 &&
		offsetof(GIFieldSamplingParameters, m_Irradiance) == 32 &&
		offsetof(GIFieldSamplingParameters, m_IrradianceInteriorWidth) == 12 &&
		offsetof(GIFieldSamplingParameters, m_ViewBias) == 44);
}
