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

		static Quaternion CreateFromYawPitchRoll(float yaw, float pitch, float roll) noexcept;
		static Quaternion CreateFromYawPitchRoll(const Vector3& angles) noexcept;
		static void FromToRotation(const Vector3& fromDir, const Vector3& toDir, Quaternion& result) noexcept;
		static Quaternion FromToRotation(const Vector3& fromDir, const Vector3& toDir) noexcept;

		static const Quaternion Identity;

		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_Z = 0.0f;
		float m_W = 1.0f;
	};

	Quaternion operator*(const Quaternion& lhs, const Quaternion& rhs) noexcept;
}
