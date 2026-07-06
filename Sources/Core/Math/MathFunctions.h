#pragma once
#include "Core/Math/MathConstants.h"
#include "Core/Math/Vector.h"

#include <cmath>
#include <concepts>

namespace gglab::math
{
	constexpr float ToRadians(float degrees) noexcept
	{
		return degrees * (Pi / 180.0f);
	}

	constexpr float ToDegrees(float radians) noexcept
	{
		return radians * (180.0f / Pi);
	}

	inline Vector4 ToVector4(const Vector3& vec3, float a) noexcept
	{
		return { vec3.m_X, vec3.m_Y, vec3.m_Z, a };
	}

	template<typename T>
	concept HasLengthSquared = requires(const T & vec)
	{
		{ vec.LengthSquared() } -> std::convertible_to<float>;
	};

	template<typename T>
	concept HasNormalize = requires(T vec)
	{
		{ vec.Normalize() };
	};

	template<typename T>
		requires HasLengthSquared<T>
	constexpr bool IsZeroVector(const T& vec, float tolerance = 1e-6f) noexcept
	{
		const float lenSq = static_cast<float>(vec.LengthSquared());
		return lenSq <= tolerance * tolerance;
	}

	template<typename T>
		requires(HasLengthSquared<T>&& HasNormalize<T>)
	constexpr T SafeNormalize(const T& vec, const T& fallback, float tolerance = 1e-6f) noexcept
	{
		if (!IsZeroVector(vec, tolerance))
		{
			T norm = vec;
			norm.Normalize();
			return norm;
		}

		return fallback;
	}

	inline bool IsFinite(float value) noexcept
	{
		return std::isfinite(value);
	}
}
