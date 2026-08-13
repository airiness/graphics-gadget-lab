#pragma once
#include "Core/CoreMacros.h"

#include <cstddef>

namespace gglab::math
{
	struct Matrix;
	struct Vector4;

	// gglab math types are plain, scalar storage types for CPU/GPU and asset interop.
	// Use TryNormalize or SafeNormalize when zero, near-zero or non-finite input is possible.
	struct Vector2
	{
		constexpr Vector2() noexcept = default;
		constexpr explicit Vector2(float value) noexcept : m_X(value), m_Y(value) {}
		constexpr Vector2(float x, float y) noexcept : m_X(x), m_Y(y) {}

		float& operator[](size_t index) noexcept
		{
			GGLAB_ASSERT(index < 2);
			return (&m_X)[index];
		}
		const float& operator[](size_t index) const noexcept
		{
			GGLAB_ASSERT(index < 2);
			return (&m_X)[index];
		}

		Vector2& operator+=(const Vector2& rhs) noexcept;
		Vector2& operator-=(const Vector2& rhs) noexcept;
		Vector2& operator*=(const Vector2& rhs) noexcept;
		Vector2& operator*=(float scalar) noexcept;
		Vector2& operator/=(float scalar) noexcept;

		Vector2 operator+() const noexcept { return *this; }
		Vector2 operator-() const noexcept { return Vector2(-m_X, -m_Y); }

		float Length() const noexcept;
		float LengthSquared() const noexcept;
		float Dot(const Vector2& rhs) const noexcept;
		void Normalize() noexcept;
		void Normalize(Vector2& result) const noexcept;

		static const Vector2 Zero;
		static const Vector2 One;
		static const Vector2 UnitX;
		static const Vector2 UnitY;

		float m_X = 0.0f;
		float m_Y = 0.0f;
	};

	struct Vector3
	{
		constexpr Vector3() noexcept = default;
		constexpr explicit Vector3(float value) noexcept : m_X(value), m_Y(value), m_Z(value) {}
		constexpr Vector3(float x, float y, float z) noexcept : m_X(x), m_Y(y), m_Z(z) {}

		float& operator[](size_t index) noexcept
		{
			GGLAB_ASSERT(index < 3);
			return (&m_X)[index];
		}
		const float& operator[](size_t index) const noexcept
		{
			GGLAB_ASSERT(index < 3);
			return (&m_X)[index];
		}

		Vector3& operator+=(const Vector3& rhs) noexcept;
		Vector3& operator-=(const Vector3& rhs) noexcept;
		Vector3& operator*=(const Vector3& rhs) noexcept;
		Vector3& operator*=(float scalar) noexcept;
		Vector3& operator/=(float scalar) noexcept;

		Vector3 operator+() const noexcept { return *this; }
		Vector3 operator-() const noexcept { return Vector3(-m_X, -m_Y, -m_Z); }

		float Length() const noexcept;
		float LengthSquared() const noexcept;
		float Dot(const Vector3& rhs) const noexcept;
		Vector3 Cross(const Vector3& rhs) const noexcept;
		void Normalize() noexcept;
		void Normalize(Vector3& result) const noexcept;
		Vector3 Normalized() const noexcept;

		static const Vector3 Zero;
		static const Vector3 One;
		static const Vector3 UnitX;
		static const Vector3 UnitY;
		static const Vector3 UnitZ;
		static const Vector3 Up;
		static const Vector3 Down;
		static const Vector3 Right;
		static const Vector3 Left;
		static const Vector3 Forward;
		static const Vector3 Backward;

		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_Z = 0.0f;
	};

	struct Vector4
	{
		constexpr Vector4() noexcept = default;
		constexpr explicit Vector4(float value) noexcept :
			m_X(value), m_Y(value), m_Z(value), m_W(value)
		{
		}
		constexpr Vector4(float x, float y, float z, float w) noexcept :
			m_X(x), m_Y(y), m_Z(z), m_W(w)
		{
		}
		constexpr Vector4(const Vector3& value, float w) noexcept :
			m_X(value.m_X), m_Y(value.m_Y), m_Z(value.m_Z), m_W(w)
		{
		}

		float& operator[](size_t index) noexcept
		{
			GGLAB_ASSERT(index < 4);
			return (&m_X)[index];
		}
		const float& operator[](size_t index) const noexcept
		{
			GGLAB_ASSERT(index < 4);
			return (&m_X)[index];
		}

		Vector4& operator+=(const Vector4& rhs) noexcept;
		Vector4& operator-=(const Vector4& rhs) noexcept;
		Vector4& operator*=(const Vector4& rhs) noexcept;
		Vector4& operator*=(float scalar) noexcept;
		Vector4& operator/=(float scalar) noexcept;

		Vector4 operator+() const noexcept { return *this; }
		Vector4 operator-() const noexcept { return Vector4(-m_X, -m_Y, -m_Z, -m_W); }

		float Length() const noexcept;
		float LengthSquared() const noexcept;
		float Dot(const Vector4& rhs) const noexcept;
		void Normalize() noexcept;
		void Normalize(Vector4& result) const noexcept;

		static const Vector4 Zero;
		static const Vector4 One;
		static const Vector4 UnitX;
		static const Vector4 UnitY;
		static const Vector4 UnitZ;
		static const Vector4 UnitW;

		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_Z = 0.0f;
		float m_W = 0.0f;
	};

	Vector2 operator+(const Vector2& lhs, const Vector2& rhs) noexcept;
	Vector2 operator-(const Vector2& lhs, const Vector2& rhs) noexcept;
	Vector2 operator*(const Vector2& lhs, const Vector2& rhs) noexcept;
	Vector2 operator*(const Vector2& lhs, float scalar) noexcept;
	Vector2 operator*(float scalar, const Vector2& rhs) noexcept;
	Vector2 operator/(const Vector2& lhs, float scalar) noexcept;
	Vector2 Min(const Vector2& lhs, const Vector2& rhs) noexcept;
	Vector2 Max(const Vector2& lhs, const Vector2& rhs) noexcept;
	Vector2 Lerp(const Vector2& lhs, const Vector2& rhs, float t) noexcept;
	[[nodiscard]] bool TryNormalize(
		const Vector2& value, Vector2& result, float tolerance = 1.0e-6f) noexcept;
	Vector2 NormalizeOr(
		const Vector2& value, const Vector2& fallback, float tolerance = 1.0e-6f) noexcept;

	Vector3 operator+(const Vector3& lhs, const Vector3& rhs) noexcept;
	Vector3 operator-(const Vector3& lhs, const Vector3& rhs) noexcept;
	Vector3 operator*(const Vector3& lhs, const Vector3& rhs) noexcept;
	Vector3 operator*(const Vector3& lhs, float scalar) noexcept;
	Vector3 operator*(float scalar, const Vector3& rhs) noexcept;
	Vector3 operator/(const Vector3& lhs, float scalar) noexcept;
	Vector3 Min(const Vector3& lhs, const Vector3& rhs) noexcept;
	Vector3 Max(const Vector3& lhs, const Vector3& rhs) noexcept;
	Vector3 Lerp(const Vector3& lhs, const Vector3& rhs, float t) noexcept;
	Vector3 TransformPoint(const Vector3& value, const Matrix& matrix) noexcept;
	Vector4 TransformPointHomogeneous(const Vector3& value, const Matrix& matrix) noexcept;
	Vector3 TransformDirection(const Vector3& value, const Matrix& matrix) noexcept;
	[[nodiscard]] bool TryNormalize(
		const Vector3& value, Vector3& result, float tolerance = 1.0e-6f) noexcept;
	Vector3 NormalizeOr(
		const Vector3& value, const Vector3& fallback, float tolerance = 1.0e-6f) noexcept;

	Vector4 operator+(const Vector4& lhs, const Vector4& rhs) noexcept;
	Vector4 operator-(const Vector4& lhs, const Vector4& rhs) noexcept;
	Vector4 operator*(const Vector4& lhs, const Vector4& rhs) noexcept;
	Vector4 operator*(const Vector4& lhs, float scalar) noexcept;
	Vector4 operator*(float scalar, const Vector4& rhs) noexcept;
	Vector4 operator/(const Vector4& lhs, float scalar) noexcept;
	Vector4 Min(const Vector4& lhs, const Vector4& rhs) noexcept;
	Vector4 Max(const Vector4& lhs, const Vector4& rhs) noexcept;
	Vector4 Lerp(const Vector4& lhs, const Vector4& rhs, float t) noexcept;
	Vector4 Transform(const Vector4& value, const Matrix& matrix) noexcept;
	[[nodiscard]] bool TryNormalize(
		const Vector4& value, Vector4& result, float tolerance = 1.0e-6f) noexcept;
	Vector4 NormalizeOr(
		const Vector4& value, const Vector4& fallback, float tolerance = 1.0e-6f) noexcept;
}

namespace gglab
{
	using math::Vector2;
	using math::Vector3;
	using math::Vector4;
}
