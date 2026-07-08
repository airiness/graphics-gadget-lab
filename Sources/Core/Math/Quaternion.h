#pragma once
#include "Core/Math/Vector.h"

namespace gglab::math
{
	struct Quaternion
	{
		constexpr Quaternion() noexcept = default;
		constexpr Quaternion(float x, float y, float z, float w) noexcept : m_X(x), m_Y(y), m_Z(z), m_W(w) {}

		Quaternion& operator*=(const Quaternion& rhs) noexcept;

		Vector3 ToEuler() const noexcept;
		void Normalize() noexcept;
		void Normalize(Quaternion& result) const noexcept;

		static const Quaternion Identity;

		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_Z = 0.0f;
		float m_W = 1.0f;
	};

	Quaternion operator*(const Quaternion& lhs, const Quaternion& rhs) noexcept;
	Quaternion CreateFromYawPitchRoll(float yaw, float pitch, float roll) noexcept;
	Quaternion CreateFromYawPitchRoll(const Vector3& angles) noexcept;
	Quaternion RotationFromTo(const Vector3& fromDirection, const Vector3& toDirection) noexcept;
	[[nodiscard]] bool TryRotationFromTo(
		const Vector3& fromDirection,
		const Vector3& toDirection,
		Quaternion& result,
		float tolerance = 1.0e-6f) noexcept;
	[[nodiscard]] bool TryNormalize(
		const Quaternion& value,
		Quaternion& result,
		float tolerance = 1.0e-6f) noexcept;
	Quaternion NormalizeOr(
		const Quaternion& value,
		const Quaternion& fallback,
		float tolerance = 1.0e-6f) noexcept;
	Quaternion Slerp(const Quaternion& lhs, const Quaternion& rhs, float t) noexcept;
}

namespace gglab
{
	using math::Quaternion;
}
