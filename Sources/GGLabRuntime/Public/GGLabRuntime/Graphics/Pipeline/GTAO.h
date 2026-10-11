#pragma once

#include "GGLabRuntime/Graphics/Pipeline/GTAOTypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureValidation.h"

#include <cstdint>

namespace gglab
{
	inline constexpr uint32_t GTAOResolutionDivisor = 2;
	inline constexpr uint32_t GTAOThreadGroupSize = 8;
	// Temporal history at half render extent: accumulated visibility with its effective
	// sample count, and the view Z of the surface each texel selected.
	inline constexpr RHIFormat GTAOHistoryVisibilityFormat = RHIFormat::R16G16Float;
	inline constexpr RHIFormat GTAOHistoryViewZFormat = RHIFormat::R32Float;

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

	struct GTAOTemporalHistoryRenderGraphResources
	{
		RGTextureId m_PreviousVisibility{};
		RGTextureId m_PreviousViewZ{};
		RGTextureId m_NextVisibility{};
		RGTextureId m_NextViewZ{};
		// False when no compatible history exists; the previous textures are not read.
		bool m_PreviousValid = false;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_PreviousVisibility.IsValid() && m_PreviousViewZ.IsValid() &&
				m_NextVisibility.IsValid() && m_NextViewZ.IsValid();
		}
	};

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
