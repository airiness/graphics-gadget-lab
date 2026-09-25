#pragma once

#include "GGLabRuntime/Graphics/Pipeline/GTAOTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureValidation.h"

#include <cstdint>

namespace gglab
{
	inline constexpr uint32_t GTAOResolutionDivisor = 2;
	inline constexpr uint32_t GTAOThreadGroupSize = 8;

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

	[[nodiscard]] constexpr GTAOExtent MakeGTAOHalfResolutionExtent(
		uint32_t fullWidth, uint32_t fullHeight) noexcept
	{
		return {
			.m_Width = (fullWidth + GTAOResolutionDivisor - 1) / GTAOResolutionDivisor,
			.m_Height = (fullHeight + GTAOResolutionDivisor - 1) / GTAOResolutionDivisor,
		};
	}

	[[nodiscard]] constexpr GTAOFinalAOFormatResolution ResolveGTAOFinalAOFormat(
		GTAOSurfaceFormatSupport preferredR8Unorm,
		GTAOSurfaceFormatSupport fallbackR16Float) noexcept
	{
		return {
			.m_PreferredR8Unorm = preferredR8Unorm,
			.m_FallbackR16Float = fallbackR16Float,
			.m_Format = preferredR8Unorm.IsSupported() ? RHIFormat::R8Unorm
				: fallbackR16Float.IsSupported() ? RHIFormat::R16Float : RHIFormat::Unknown,
		};
	}
}
