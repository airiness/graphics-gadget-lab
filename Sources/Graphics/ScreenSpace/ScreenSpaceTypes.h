#pragma once
#include "Core/Math/Matrix.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace gglab
{
	enum class DepthConvention : uint8_t
	{
		Reversed = 0,
		Standard = 1,
	};

	static_assert(std::to_underlying(DepthConvention::Reversed) == 0);
	static_assert(std::to_underlying(DepthConvention::Standard) == 1);

	namespace screen_space
	{
		inline constexpr float GetDepthBackgroundValue(DepthConvention convention) noexcept
		{
			return convention == DepthConvention::Reversed ? 0.0f : 1.0f;
		}

		inline constexpr float GetDepthNearValue(DepthConvention convention) noexcept
		{
			return convention == DepthConvention::Reversed ? 1.0f : 0.0f;
		}

		inline constexpr float GetDepthFarValue(DepthConvention convention) noexcept
		{
			return GetDepthBackgroundValue(convention);
		}

		inline constexpr bool IsDepthBackground(float rawDepth, DepthConvention convention) noexcept
		{
			return convention == DepthConvention::Reversed ? rawDepth <= 0.0f : rawDepth >= 1.0f;
		}

		inline constexpr bool IsDepthNearer(
			float lhs, float rhs, DepthConvention convention) noexcept
		{
			return convention == DepthConvention::Reversed ? lhs > rhs : lhs < rhs;
		}

		inline constexpr bool IsDepthFarther(
			float lhs, float rhs, DepthConvention convention) noexcept
		{
			return convention == DepthConvention::Reversed ? lhs < rhs : lhs > rhs;
		}

		inline Vector2 PixelCenterToUV(
			uint32_t pixelX, uint32_t pixelY, uint32_t width, uint32_t height) noexcept
		{
			GGLAB_ASSERT(width > 0 && height > 0);
			width = std::max(width, 1u);
			height = std::max(height, 1u);
			return Vector2((static_cast<float>(pixelX) + 0.5f) / static_cast<float>(width),
				(static_cast<float>(pixelY) + 0.5f) / static_cast<float>(height));
		}

		inline constexpr Vector2 UVToNDC(const Vector2& uv) noexcept
		{
			return Vector2(uv.m_X * 2.0f - 1.0f, 1.0f - uv.m_Y * 2.0f);
		}

		inline constexpr Vector2 NDCToUV(const Vector2& ndc) noexcept
		{
			return Vector2(ndc.m_X * 0.5f + 0.5f, 0.5f - ndc.m_Y * 0.5f);
		}

		inline Vector3 ReconstructPositionFromRawDepth(
			const Vector2& uv, float rawDepth, const Matrix& inverseTransform) noexcept
		{
			const Vector2 ndc = UVToNDC(uv);
			const Vector4 homogeneousPosition =
				math::Transform(Vector4(ndc.m_X, ndc.m_Y, rawDepth, 1.0f), inverseTransform);
			if (!std::isfinite(homogeneousPosition.m_X) ||
				!std::isfinite(homogeneousPosition.m_Y) ||
				!std::isfinite(homogeneousPosition.m_Z) ||
				!std::isfinite(homogeneousPosition.m_W) ||
				std::abs(homogeneousPosition.m_W) <= 1.0e-8f)
			{
				return Vector3::Zero;
			}

			const float inverseW = 1.0f / homogeneousPosition.m_W;
			return Vector3(homogeneousPosition.m_X * inverseW, homogeneousPosition.m_Y * inverseW,
				homogeneousPosition.m_Z * inverseW);
		}

		inline Vector3 ReconstructViewPosition(
			const Vector2& uv, float rawDepth, const Matrix& inverseProjection) noexcept
		{
			return ReconstructPositionFromRawDepth(uv, rawDepth, inverseProjection);
		}

		inline Vector3 ReconstructWorldPosition(
			const Vector2& uv, float rawDepth, const Matrix& inverseViewProjection) noexcept
		{
			return ReconstructPositionFromRawDepth(uv, rawDepth, inverseViewProjection);
		}

		inline float RawDepthToPositiveViewZ(
			const Vector2& uv, float rawDepth, const Matrix& inverseProjection) noexcept
		{
			return std::max(ReconstructViewPosition(uv, rawDepth, inverseProjection).m_Z, 0.0f);
		}
	}
}
