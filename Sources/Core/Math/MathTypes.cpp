#include "Core/Precompiled.h"
#include "Core/Math/MathTypes.h"

namespace gglab::math
{
	const Vector2 Vector2::Zero = { 0.0f, 0.0f };
	const Vector2 Vector2::One = { 1.0f, 1.0f };
	const Vector2 Vector2::UnitX = { 1.0f, 0.0f };
	const Vector2 Vector2::UnitY = { 0.0f, 1.0f };

	const Vector3 Vector3::Zero = { 0.0f, 0.0f, 0.0f };
	const Vector3 Vector3::One = { 1.0f, 1.0f, 1.0f };
	const Vector3 Vector3::UnitX = { 1.0f, 0.0f, 0.0f };
	const Vector3 Vector3::UnitY = { 0.0f, 1.0f, 0.0f };
	const Vector3 Vector3::UnitZ = { 0.0f, 0.0f, 1.0f };
	const Vector3 Vector3::Up = { 0.0f, 1.0f, 0.0f };
	const Vector3 Vector3::Down = { 0.0f, -1.0f, 0.0f };
	const Vector3 Vector3::Right = { 1.0f, 0.0f, 0.0f };
	const Vector3 Vector3::Left = { -1.0f, 0.0f, 0.0f };
	const Vector3 Vector3::Forward = { 0.0f, 0.0f, -1.0f };
	const Vector3 Vector3::Backward = { 0.0f, 0.0f, 1.0f };

	const Vector4 Vector4::Zero = { 0.0f, 0.0f, 0.0f, 0.0f };
	const Vector4 Vector4::One = { 1.0f, 1.0f, 1.0f, 1.0f };
	const Vector4 Vector4::UnitX = { 1.0f, 0.0f, 0.0f, 0.0f };
	const Vector4 Vector4::UnitY = { 0.0f, 1.0f, 0.0f, 0.0f };
	const Vector4 Vector4::UnitZ = { 0.0f, 0.0f, 1.0f, 0.0f };
	const Vector4 Vector4::UnitW = { 0.0f, 0.0f, 0.0f, 1.0f };

	const Matrix Matrix::Identity =
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
	};

	const Quaternion Quaternion::Identity = { 0.0f, 0.0f, 0.0f, 1.0f };

	Vector2& Vector2::operator+=(const Vector2& rhs) noexcept
	{
		x += rhs.x;
		y += rhs.y;
		return *this;
	}

	Vector2& Vector2::operator-=(const Vector2& rhs) noexcept
	{
		x -= rhs.x;
		y -= rhs.y;
		return *this;
	}

	Vector2& Vector2::operator*=(const Vector2& rhs) noexcept
	{
		x *= rhs.x;
		y *= rhs.y;
		return *this;
	}

	Vector2& Vector2::operator*=(float scalar) noexcept
	{
		x *= scalar;
		y *= scalar;
		return *this;
	}

	Vector2& Vector2::operator/=(float scalar) noexcept
	{
		x /= scalar;
		y /= scalar;
		return *this;
	}

	Vector3& Vector3::operator+=(const Vector3& rhs) noexcept
	{
		x += rhs.x;
		y += rhs.y;
		z += rhs.z;
		return *this;
	}

	Vector3& Vector3::operator-=(const Vector3& rhs) noexcept
	{
		x -= rhs.x;
		y -= rhs.y;
		z -= rhs.z;
		return *this;
	}

	Vector3& Vector3::operator*=(const Vector3& rhs) noexcept
	{
		x *= rhs.x;
		y *= rhs.y;
		z *= rhs.z;
		return *this;
	}

	Vector3& Vector3::operator*=(float scalar) noexcept
	{
		x *= scalar;
		y *= scalar;
		z *= scalar;
		return *this;
	}

	Vector3& Vector3::operator/=(float scalar) noexcept
	{
		x /= scalar;
		y /= scalar;
		z /= scalar;
		return *this;
	}

	Vector4& Vector4::operator+=(const Vector4& rhs) noexcept
	{
		x += rhs.x;
		y += rhs.y;
		z += rhs.z;
		w += rhs.w;
		return *this;
	}

	Vector4& Vector4::operator-=(const Vector4& rhs) noexcept
	{
		x -= rhs.x;
		y -= rhs.y;
		z -= rhs.z;
		w -= rhs.w;
		return *this;
	}

	Vector4& Vector4::operator*=(const Vector4& rhs) noexcept
	{
		x *= rhs.x;
		y *= rhs.y;
		z *= rhs.z;
		w *= rhs.w;
		return *this;
	}

	Vector4& Vector4::operator*=(float scalar) noexcept
	{
		x *= scalar;
		y *= scalar;
		z *= scalar;
		w *= scalar;
		return *this;
	}

	Vector4& Vector4::operator/=(float scalar) noexcept
	{
		x /= scalar;
		y /= scalar;
		z /= scalar;
		w /= scalar;
		return *this;
	}

	Vector3 Matrix::Translation() const noexcept
	{
		return Vector3(_41, _42, _43);
	}

	void Matrix::Translation(const Vector3& value) noexcept
	{
		_41 = value.x;
		_42 = value.y;
		_43 = value.z;
	}

	Vector2 operator+(const Vector2& lhs, const Vector2& rhs) noexcept
	{
		Vector2 result = lhs;
		result += rhs;
		return result;
	}

	Vector2 operator-(const Vector2& lhs, const Vector2& rhs) noexcept
	{
		Vector2 result = lhs;
		result -= rhs;
		return result;
	}

	Vector2 operator*(const Vector2& lhs, const Vector2& rhs) noexcept
	{
		Vector2 result = lhs;
		result *= rhs;
		return result;
	}

	Vector2 operator*(const Vector2& lhs, float scalar) noexcept
	{
		Vector2 result = lhs;
		result *= scalar;
		return result;
	}

	Vector2 operator*(float scalar, const Vector2& rhs) noexcept
	{
		return rhs * scalar;
	}

	Vector2 operator/(const Vector2& lhs, float scalar) noexcept
	{
		Vector2 result = lhs;
		result /= scalar;
		return result;
	}

	Vector3 operator+(const Vector3& lhs, const Vector3& rhs) noexcept
	{
		Vector3 result = lhs;
		result += rhs;
		return result;
	}

	Vector3 operator-(const Vector3& lhs, const Vector3& rhs) noexcept
	{
		Vector3 result = lhs;
		result -= rhs;
		return result;
	}

	Vector3 operator*(const Vector3& lhs, const Vector3& rhs) noexcept
	{
		Vector3 result = lhs;
		result *= rhs;
		return result;
	}

	Vector3 operator*(const Vector3& lhs, float scalar) noexcept
	{
		Vector3 result = lhs;
		result *= scalar;
		return result;
	}

	Vector3 operator*(float scalar, const Vector3& rhs) noexcept
	{
		return rhs * scalar;
	}

	Vector3 operator/(const Vector3& lhs, float scalar) noexcept
	{
		Vector3 result = lhs;
		result /= scalar;
		return result;
	}

	Vector4 operator+(const Vector4& lhs, const Vector4& rhs) noexcept
	{
		Vector4 result = lhs;
		result += rhs;
		return result;
	}

	Vector4 operator-(const Vector4& lhs, const Vector4& rhs) noexcept
	{
		Vector4 result = lhs;
		result -= rhs;
		return result;
	}

	Vector4 operator*(const Vector4& lhs, const Vector4& rhs) noexcept
	{
		Vector4 result = lhs;
		result *= rhs;
		return result;
	}

	Vector4 operator*(const Vector4& lhs, float scalar) noexcept
	{
		Vector4 result = lhs;
		result *= scalar;
		return result;
	}

	Vector4 operator*(float scalar, const Vector4& rhs) noexcept
	{
		return rhs * scalar;
	}

	Vector4 operator/(const Vector4& lhs, float scalar) noexcept
	{
		Vector4 result = lhs;
		result /= scalar;
		return result;
	}

}
