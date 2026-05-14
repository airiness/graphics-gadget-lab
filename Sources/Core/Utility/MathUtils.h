#pragma once
#include "Core/Utility/TypeUtils.h"

namespace gglab::utils
{
	Vector4 ToVector4(const Vector3& vec3, float a) noexcept;
	constexpr float ToRadians(float degrees) noexcept
	{
		return DirectX::XMConvertToRadians(degrees);
	}

	constexpr float ToDegrees(float radians) noexcept
	{
		return DirectX::XMConvertToDegrees(radians);
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

	inline bool IsFinite(float v) noexcept
	{
		return std::isfinite(v);
	}

	template<UnsignedInteger T>
	constexpr T AlignUp(T value, T multiple) noexcept
	{
		if (multiple == 0)
		{
			return value;
		}
		const T result = value % multiple;
		return result ? (value + multiple - result) : value;
	}

	template<UnsignedInteger T>
	constexpr T AlignDown(T value, T multiple) noexcept
	{
		if (multiple == 0)
		{
			return value;
		}
		return value - (value % multiple);
	}

	template<UnsignedInteger T>
	constexpr bool IsPow2(T value) noexcept
	{
		return (value != 0) && ((value & (value - 1)) == 0);
	}

	template<UnsignedInteger T>
	constexpr T AlignUpPow2(T value, T alignment) noexcept
	{
		GGLAB_ASSERT_MSG(IsPow2(alignment), "Alignment must be a power of two.");
		return (alignment == 0) ? value : ((value + alignment - 1) & ~(alignment - 1));
	}
}