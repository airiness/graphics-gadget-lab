#pragma once

#include <cstddef>

namespace gglab::math
{
	inline constexpr float Pi = 3.14159265358979323846f;
	inline constexpr float TwoPi = Pi * 2.0f;

	struct Vector2
	{
		constexpr Vector2() noexcept = default;
		constexpr explicit Vector2(float value) noexcept : x(value), y(value) {}
		constexpr Vector2(float x, float y) noexcept : x(x), y(y) {}

		float& operator[](size_t index) noexcept { return (&x)[index]; }
		const float& operator[](size_t index) const noexcept { return (&x)[index]; }

		Vector2& operator+=(const Vector2& rhs) noexcept;
		Vector2& operator-=(const Vector2& rhs) noexcept;
		Vector2& operator*=(const Vector2& rhs) noexcept;
		Vector2& operator*=(float scalar) noexcept;
		Vector2& operator/=(float scalar) noexcept;

		Vector2 operator+() const noexcept { return *this; }
		Vector2 operator-() const noexcept { return Vector2(-x, -y); }

		float Length() const noexcept;
		float LengthSquared() const noexcept;
		float Dot(const Vector2& rhs) const noexcept;
		void Normalize() noexcept;
		void Normalize(Vector2& result) const noexcept;

		static Vector2 Min(const Vector2& lhs, const Vector2& rhs) noexcept;
		static Vector2 Max(const Vector2& lhs, const Vector2& rhs) noexcept;
		static Vector2 Lerp(const Vector2& lhs, const Vector2& rhs, float t) noexcept;

		static const Vector2 Zero;
		static const Vector2 One;
		static const Vector2 UnitX;
		static const Vector2 UnitY;

		float x = 0.0f;
		float y = 0.0f;
	};

	struct Matrix;
	struct Quaternion;
	struct Vector4;

	struct Vector3
	{
		constexpr Vector3() noexcept = default;
		constexpr explicit Vector3(float value) noexcept : x(value), y(value), z(value) {}
		constexpr Vector3(float x, float y, float z) noexcept : x(x), y(y), z(z) {}

		float& operator[](size_t index) noexcept { return (&x)[index]; }
		const float& operator[](size_t index) const noexcept { return (&x)[index]; }

		Vector3& operator+=(const Vector3& rhs) noexcept;
		Vector3& operator-=(const Vector3& rhs) noexcept;
		Vector3& operator*=(const Vector3& rhs) noexcept;
		Vector3& operator*=(float scalar) noexcept;
		Vector3& operator/=(float scalar) noexcept;

		Vector3 operator+() const noexcept { return *this; }
		Vector3 operator-() const noexcept { return Vector3(-x, -y, -z); }

		float Length() const noexcept;
		float LengthSquared() const noexcept;
		float Dot(const Vector3& rhs) const noexcept;
		Vector3 Cross(const Vector3& rhs) const noexcept;
		void Normalize() noexcept;
		void Normalize(Vector3& result) const noexcept;
		Vector3 Normalized() const noexcept;

		static Vector3 Min(const Vector3& lhs, const Vector3& rhs) noexcept;
		static Vector3 Max(const Vector3& lhs, const Vector3& rhs) noexcept;
		static Vector3 Lerp(const Vector3& lhs, const Vector3& rhs, float t) noexcept;
		static Vector3 Transform(const Vector3& value, const Matrix& matrix) noexcept;
		static void Transform(const Vector3& value, const Matrix& matrix, Vector4& result) noexcept;
		static Vector3 TransformNormal(const Vector3& value, const Matrix& matrix) noexcept;

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

		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	struct Vector4
	{
		constexpr Vector4() noexcept = default;
		constexpr explicit Vector4(float value) noexcept : x(value), y(value), z(value), w(value) {}
		constexpr Vector4(float x, float y, float z, float w) noexcept : x(x), y(y), z(z), w(w) {}
		constexpr Vector4(const Vector3& value, float w) noexcept : x(value.x), y(value.y), z(value.z), w(w) {}

		float& operator[](size_t index) noexcept { return (&x)[index]; }
		const float& operator[](size_t index) const noexcept { return (&x)[index]; }

		Vector4& operator+=(const Vector4& rhs) noexcept;
		Vector4& operator-=(const Vector4& rhs) noexcept;
		Vector4& operator*=(const Vector4& rhs) noexcept;
		Vector4& operator*=(float scalar) noexcept;
		Vector4& operator/=(float scalar) noexcept;

		Vector4 operator+() const noexcept { return *this; }
		Vector4 operator-() const noexcept { return Vector4(-x, -y, -z, -w); }

		float Length() const noexcept;
		float LengthSquared() const noexcept;
		float Dot(const Vector4& rhs) const noexcept;
		void Normalize() noexcept;
		void Normalize(Vector4& result) const noexcept;

		static Vector4 Min(const Vector4& lhs, const Vector4& rhs) noexcept;
		static Vector4 Max(const Vector4& lhs, const Vector4& rhs) noexcept;
		static Vector4 Lerp(const Vector4& lhs, const Vector4& rhs, float t) noexcept;
		static Vector4 Transform(const Vector4& value, const Matrix& matrix) noexcept;

		static const Vector4 Zero;
		static const Vector4 One;
		static const Vector4 UnitX;
		static const Vector4 UnitY;
		static const Vector4 UnitZ;
		static const Vector4 UnitW;

		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 0.0f;
	};

	struct Color
	{
		constexpr Color() noexcept = default;
		constexpr explicit Color(float value) noexcept : x(value), y(value), z(value), w(value) {}
		constexpr Color(float r, float g, float b, float a) noexcept : x(r), y(g), z(b), w(a) {}

		float R() const noexcept { return x; }
		float G() const noexcept { return y; }
		float B() const noexcept { return z; }
		float A() const noexcept { return w; }
		constexpr operator Vector4() const noexcept { return Vector4(x, y, z, w); }

		float& operator[](size_t index) noexcept { return (&x)[index]; }
		const float& operator[](size_t index) const noexcept { return (&x)[index]; }

		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 1.0f;
	};

	struct Matrix
	{
		constexpr Matrix() noexcept = default;
		constexpr Matrix(
			float m00, float m01, float m02, float m03,
			float m10, float m11, float m12, float m13,
			float m20, float m21, float m22, float m23,
			float m30, float m31, float m32, float m33) noexcept :
			_11(m00), _12(m01), _13(m02), _14(m03),
			_21(m10), _22(m11), _23(m12), _24(m13),
			_31(m20), _32(m21), _33(m22), _34(m23),
			_41(m30), _42(m31), _43(m32), _44(m33)
		{
		}

		Matrix& operator*=(const Matrix& rhs) noexcept;

		Matrix Invert() const noexcept;
		void Invert(Matrix& result) const noexcept;
		Matrix Transpose() const noexcept;
		void Transpose(Matrix& result) const noexcept;
		Vector3 Translation() const noexcept;
		void Translation(const Vector3& value) noexcept;

		static Matrix CreateScale(const Vector3& scale) noexcept;
		static Matrix CreateScale(float scale) noexcept;
		static Matrix CreateTranslation(const Vector3& translation) noexcept;
		static Matrix CreateFromQuaternion(const Quaternion& rotation) noexcept;
		static Matrix CreateLookAt(const Vector3& eye, const Vector3& target, const Vector3& up) noexcept;
		static Matrix CreatePerspectiveFieldOfView(float fovRadians, float aspect, float nearZ, float farZ) noexcept;
		static Matrix CreateOrthographicOffCenter(float left, float right, float bottom, float top, float nearZ, float farZ) noexcept;

		static const Matrix Identity;

		union
		{
			struct
			{
				float _11, _12, _13, _14;
				float _21, _22, _23, _24;
				float _31, _32, _33, _34;
				float _41, _42, _43, _44;
			};
			float m[4][4] = {};
		};
	};

	struct Quaternion
	{
		constexpr Quaternion() noexcept = default;
		constexpr Quaternion(float x, float y, float z, float w) noexcept : x(x), y(y), z(z), w(w) {}

		Quaternion& operator*=(const Quaternion& rhs) noexcept;

		Vector3 ToEuler() const noexcept;
		void Normalize() noexcept;
		void Normalize(Quaternion& result) const noexcept;

		static Quaternion CreateFromYawPitchRoll(float yaw, float pitch, float roll) noexcept;
		static Quaternion CreateFromYawPitchRoll(const Vector3& angles) noexcept;
		static void FromToRotation(const Vector3& fromDir, const Vector3& toDir, Quaternion& result) noexcept;
		static Quaternion FromToRotation(const Vector3& fromDir, const Vector3& toDir) noexcept;

		static const Quaternion Identity;

		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 1.0f;
	};

	struct BoundingBox
	{
		constexpr BoundingBox() noexcept = default;
		constexpr BoundingBox(const Vector3& center, const Vector3& extents) noexcept :
			Center(center),
			Extents(extents)
		{
		}

		void Transform(BoundingBox& result, const Matrix& matrix) const noexcept;

		static void CreateFromPoints(BoundingBox& result, size_t count, const Vector3* points, size_t stride) noexcept;
		static void CreateMerged(BoundingBox& result, const BoundingBox& lhs, const BoundingBox& rhs) noexcept;

		Vector3 Center = Vector3::Zero;
		Vector3 Extents = Vector3::Zero;
	};

	struct BoundingSphere
	{
		constexpr BoundingSphere() noexcept = default;
		constexpr BoundingSphere(const Vector3& center, float radius) noexcept :
			Center(center),
			Radius(radius)
		{
		}

		void Transform(BoundingSphere& result, const Matrix& matrix) const noexcept;

		static void CreateFromPoints(BoundingSphere& result, size_t count, const Vector3* points, size_t stride) noexcept;
		static void CreateFromBoundingBox(BoundingSphere& result, const BoundingBox& box) noexcept;

		Vector3 Center = Vector3::Zero;
		float Radius = 0.0f;
	};

	Vector2 operator+(const Vector2& lhs, const Vector2& rhs) noexcept;
	Vector2 operator-(const Vector2& lhs, const Vector2& rhs) noexcept;
	Vector2 operator*(const Vector2& lhs, const Vector2& rhs) noexcept;
	Vector2 operator*(const Vector2& lhs, float scalar) noexcept;
	Vector2 operator*(float scalar, const Vector2& rhs) noexcept;
	Vector2 operator/(const Vector2& lhs, float scalar) noexcept;

	Vector3 operator+(const Vector3& lhs, const Vector3& rhs) noexcept;
	Vector3 operator-(const Vector3& lhs, const Vector3& rhs) noexcept;
	Vector3 operator*(const Vector3& lhs, const Vector3& rhs) noexcept;
	Vector3 operator*(const Vector3& lhs, float scalar) noexcept;
	Vector3 operator*(float scalar, const Vector3& rhs) noexcept;
	Vector3 operator/(const Vector3& lhs, float scalar) noexcept;

	Vector4 operator+(const Vector4& lhs, const Vector4& rhs) noexcept;
	Vector4 operator-(const Vector4& lhs, const Vector4& rhs) noexcept;
	Vector4 operator*(const Vector4& lhs, const Vector4& rhs) noexcept;
	Vector4 operator*(const Vector4& lhs, float scalar) noexcept;
	Vector4 operator*(float scalar, const Vector4& rhs) noexcept;
	Vector4 operator/(const Vector4& lhs, float scalar) noexcept;

	Matrix operator*(const Matrix& lhs, const Matrix& rhs) noexcept;
	Quaternion operator*(const Quaternion& lhs, const Quaternion& rhs) noexcept;
}

namespace gglab
{
	using math::Color;
	using math::BoundingBox;
	using math::BoundingSphere;
	using math::Matrix;
	using math::Quaternion;
	using math::Vector2;
	using math::Vector3;
	using math::Vector4;
}
