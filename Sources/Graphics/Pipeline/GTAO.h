#pragma once

#include "Graphics/ScreenSpace/ScreenSpaceTypes.h"

#include <array>
#include <cstdint>

namespace gglab
{
	inline constexpr uint32_t GTAOResolutionDivisor = 2;
	inline constexpr uint32_t GTAOThreadGroupSize = 8;
	inline constexpr uint32_t GTAOMaxDirectionCount = 8;
	inline constexpr uint32_t GTAOMaxStepCount = 8;

	struct GTAOExtent
	{
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_Width > 0 && m_Height > 0;
		}

		bool operator==(const GTAOExtent&) const noexcept = default;
	};

	struct GTAOSurfaceCandidate
	{
		float m_RawDepth = 0.0f;
		float m_ViewZ = 0.0f;
	};

	struct GTAOSurfaceSelection
	{
		uint32_t m_SelectedIndex = 0;
		float m_RawDepth = 0.0f;
		float m_ViewZ = 0.0f;
		bool m_IsValid = false;
	};

	[[nodiscard]] constexpr GTAOExtent MakeGTAOHalfResolutionExtent(
		uint32_t fullWidth, uint32_t fullHeight) noexcept
	{
		return {
			.m_Width = (fullWidth + GTAOResolutionDivisor - 1) / GTAOResolutionDivisor,
			.m_Height = (fullHeight + GTAOResolutionDivisor - 1) / GTAOResolutionDivisor,
		};
	}

	// Candidates are ordered top-left, top-right, bottom-left, bottom-right.
	// A strict nearer comparison intentionally preserves the first candidate on equal depth.
	[[nodiscard]] GTAOSurfaceSelection SelectGTAOHalfResolutionSurface(
		const std::array<GTAOSurfaceCandidate, 4>& candidates,
		DepthConvention convention) noexcept;

	[[nodiscard]] float GTAOInterleavedGradientNoise(uint32_t pixelX, uint32_t pixelY) noexcept;
}
