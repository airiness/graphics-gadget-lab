#pragma once

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
	concept UnsignedInteger = std::is_unsigned_v<T> && std::is_integral_v<T>;

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