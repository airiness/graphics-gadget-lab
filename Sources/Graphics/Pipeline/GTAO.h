#pragma once

#include "Graphics/ScreenSpace/ScreenSpaceTypes.h"
#include "Graphics/RHI/RHITextureValidation.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace gglab
{
	inline constexpr uint32_t GTAOResolutionDivisor = 2;
	inline constexpr uint32_t GTAOThreadGroupSize = 8;
	inline constexpr uint32_t GTAOMaxDirectionCount = 8;
	inline constexpr uint32_t GTAOMaxStepCount = 8;
	inline constexpr uint32_t GTAOMaxDenoiseRadius = 8;

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

	struct GTAONormalAxisNeighborAvailability
	{
		bool m_HasNegativeNeighbor = false;
		bool m_HasPositiveNeighbor = false;
	};

	struct GTAOSurfaceFormatSupport
	{
		RHITextureSupportResult m_ShaderResource{};
		RHITextureSupportResult m_TypedUavStore{};

		[[nodiscard]] constexpr bool IsSupported() const noexcept
		{
			return m_ShaderResource.IsSupported() && m_TypedUavStore.IsSupported();
		}
	};

	struct GTAOFinalAOFormatResolution
	{
		GTAOSurfaceFormatSupport m_PreferredR8Unorm{};
		GTAOSurfaceFormatSupport m_FallbackR16Float{};
		RHIFormat m_Format = RHIFormat::Unknown;

		[[nodiscard]] constexpr bool IsAvailable() const noexcept
		{
			return m_Format != RHIFormat::Unknown;
		}

		[[nodiscard]] constexpr bool UsesFallback() const noexcept
		{
			return m_Format == RHIFormat::R16Float;
		}
	};

	struct GTAOCapabilityStatus
	{
		GTAOSurfaceFormatSupport m_R16Float{};
		GTAOSurfaceFormatSupport m_R32Float{};
		GTAOSurfaceFormatSupport m_R16G16Float{};
		GTAOSurfaceFormatSupport m_R16G16B16A16Float{};
		GTAOFinalAOFormatResolution m_FinalAO{};

		[[nodiscard]] constexpr bool IsCoreAvailable() const noexcept
		{
			return m_R16Float.IsSupported() && m_R32Float.IsSupported() &&
				m_FinalAO.IsAvailable();
		}

		[[nodiscard]] constexpr bool AreDiagnosticOutputsAvailable() const noexcept
		{
			return m_R16G16Float.IsSupported() && m_R16G16B16A16Float.IsSupported();
		}
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
	[[nodiscard]] constexpr float ResolveGTAODiffuseIBLVisibility(
		float materialAO, float gtao) noexcept
	{
		return std::clamp(materialAO * gtao, 0.0f, 1.0f);
	}

	[[nodiscard]] constexpr float ResolveGTAOSpecularIBLVisibility(float materialAO) noexcept
	{
		return std::clamp(materialAO, 0.0f, 1.0f);
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

	[[nodiscard]] constexpr GTAONormalAxisNeighborAvailability
		GetGTAONormalAxisNeighborAvailability(uint32_t centerCoordinate, uint32_t extent) noexcept
	{
		return {
			.m_HasNegativeNeighbor = centerCoordinate > 0 && centerCoordinate < extent,
			.m_HasPositiveNeighbor = centerCoordinate < extent && centerCoordinate + 1 < extent,
		};
	}
}
