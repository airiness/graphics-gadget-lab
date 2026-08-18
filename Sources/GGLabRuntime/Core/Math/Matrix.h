#pragma once
#include "Core/Math/Vector.h"

#include <type_traits>
#include <array>

namespace gglab::math
{
	struct Quaternion;

	// Math convention:
	// - gglab uses a left-handed world: +X right, +Y up, +Z forward.
	// - Matrices are stored in row-major field order and composed for row-vector use.
	// - Points and directions are transformed as v * M; compose local-to-world as S * R * T.
	// - Camera projection uses the D3D clip-space depth range [0, 1].
	struct Matrix
	{
		constexpr Matrix() noexcept = default;
		constexpr Matrix(
			float m00, float m01, float m02, float m03,
			float m10, float m11, float m12, float m13,
			float m20, float m21, float m22, float m23,
			float m30, float m31, float m32, float m33) noexcept :
			m_11(m00), m_12(m01), m_13(m02), m_14(m03),
			m_21(m10), m_22(m11), m_23(m12), m_24(m13),
			m_31(m20), m_32(m21), m_33(m22), m_34(m23),
			m_41(m30), m_42(m31), m_43(m32), m_44(m33)
		{
		}

		Matrix& operator*=(const Matrix& rhs) noexcept;

		Vector3 Translation() const noexcept;
		void Translation(const Vector3& value) noexcept;

		[[nodiscard]] constexpr std::array<float, 16> ToArray() const noexcept
		{
			return {
				m_11, m_12, m_13, m_14,
				m_21, m_22, m_23, m_24,
				m_31, m_32, m_33, m_34,
				m_41, m_42, m_43, m_44
			};
		}

		static const Matrix Identity;

		float m_11{}, m_12{}, m_13{}, m_14{};
		float m_21{}, m_22{}, m_23{}, m_24{};
		float m_31{}, m_32{}, m_33{}, m_34{};
		float m_41{}, m_42{}, m_43{}, m_44{};
	};

	static_assert(std::is_standard_layout_v<Matrix>);
	static_assert(std::is_trivially_copyable_v<Matrix>);
	static_assert(sizeof(Matrix) == 16 * sizeof(float));

	static_assert(offsetof(Matrix, m_11) == 0);
	static_assert(offsetof(Matrix, m_21) == 4 * sizeof(float));
	static_assert(offsetof(Matrix, m_31) == 8 * sizeof(float));
	static_assert(offsetof(Matrix, m_41) == 12 * sizeof(float));
	static_assert(offsetof(Matrix, m_44) == 15 * sizeof(float));

	Matrix operator*(const Matrix& lhs, const Matrix& rhs) noexcept;
	Matrix Inverse(const Matrix& matrix) noexcept;
	[[nodiscard]] bool TryInverse(
		const Matrix& matrix, Matrix& result, float determinantTolerance = 1.0e-8f) noexcept;
	Matrix SafeInverse(const Matrix& matrix, const Matrix& fallback,
		float determinantTolerance = 1.0e-8f) noexcept;
	Matrix SafeInverse(const Matrix& matrix, float determinantTolerance = 1.0e-8f) noexcept;
	Matrix Transpose(const Matrix& matrix) noexcept;
	Matrix CreateScale(const Vector3& scale) noexcept;
	Matrix CreateScale(float scale) noexcept;
	Matrix CreateTranslation(const Vector3& translation) noexcept;
	Matrix CreateFromQuaternion(const Quaternion& rotation) noexcept;
	Matrix CreateLookAtLH(const Vector3& eye, const Vector3& target, const Vector3& up) noexcept;
	Matrix CreatePerspectiveFieldOfViewLH(
		float fovRadians, float aspect, float nearZ, float farZ) noexcept;
	Matrix CreatePerspectiveFieldOfViewLHReversedZ(
		float fovRadians, float aspect, float nearZ, float farZ) noexcept;
	Matrix CreateOrthographicOffCenterLH(
		float left, float right, float bottom, float top, float nearZ, float farZ) noexcept;
}

namespace gglab
{
	using math::Matrix;
}
