#include "Core/Math/Vector.h"

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
	// gglab uses a left-handed world convention: +X right, +Y up, +Z forward.
	const Vector3 Vector3::Forward = { 0.0f, 0.0f, 1.0f };
	const Vector3 Vector3::Backward = { 0.0f, 0.0f, -1.0f };

	const Vector4 Vector4::Zero = { 0.0f, 0.0f, 0.0f, 0.0f };
	const Vector4 Vector4::One = { 1.0f, 1.0f, 1.0f, 1.0f };
	const Vector4 Vector4::UnitX = { 1.0f, 0.0f, 0.0f, 0.0f };
	const Vector4 Vector4::UnitY = { 0.0f, 1.0f, 0.0f, 0.0f };
	const Vector4 Vector4::UnitZ = { 0.0f, 0.0f, 1.0f, 0.0f };
	const Vector4 Vector4::UnitW = { 0.0f, 0.0f, 0.0f, 1.0f };

	Vector2& Vector2::operator+=(const Vector2& rhs) noexcept
	{
		m_X += rhs.m_X;
		m_Y += rhs.m_Y;
		return *this;
	}

	Vector2& Vector2::operator-=(const Vector2& rhs) noexcept
	{
		m_X -= rhs.m_X;
		m_Y -= rhs.m_Y;
		return *this;
	}

	Vector2& Vector2::operator*=(const Vector2& rhs) noexcept
	{
		m_X *= rhs.m_X;
		m_Y *= rhs.m_Y;
		return *this;
	}

	Vector2& Vector2::operator*=(float scalar) noexcept
	{
		m_X *= scalar;
		m_Y *= scalar;
		return *this;
	}

	Vector2& Vector2::operator/=(float scalar) noexcept
	{
		m_X /= scalar;
		m_Y /= scalar;
		return *this;
	}

	Vector3& Vector3::operator+=(const Vector3& rhs) noexcept
	{
		m_X += rhs.m_X;
		m_Y += rhs.m_Y;
		m_Z += rhs.m_Z;
		return *this;
	}

	Vector3& Vector3::operator-=(const Vector3& rhs) noexcept
	{
		m_X -= rhs.m_X;
		m_Y -= rhs.m_Y;
		m_Z -= rhs.m_Z;
		return *this;
	}

	Vector3& Vector3::operator*=(const Vector3& rhs) noexcept
	{
		m_X *= rhs.m_X;
		m_Y *= rhs.m_Y;
		m_Z *= rhs.m_Z;
		return *this;
	}

	Vector3& Vector3::operator*=(float scalar) noexcept
	{
		m_X *= scalar;
		m_Y *= scalar;
		m_Z *= scalar;
		return *this;
	}

	Vector3& Vector3::operator/=(float scalar) noexcept
	{
		m_X /= scalar;
		m_Y /= scalar;
		m_Z /= scalar;
		return *this;
	}

	Vector4& Vector4::operator+=(const Vector4& rhs) noexcept
	{
		m_X += rhs.m_X;
		m_Y += rhs.m_Y;
		m_Z += rhs.m_Z;
		m_W += rhs.m_W;
		return *this;
	}

	Vector4& Vector4::operator-=(const Vector4& rhs) noexcept
	{
		m_X -= rhs.m_X;
		m_Y -= rhs.m_Y;
		m_Z -= rhs.m_Z;
		m_W -= rhs.m_W;
		return *this;
	}

	Vector4& Vector4::operator*=(const Vector4& rhs) noexcept
	{
		m_X *= rhs.m_X;
		m_Y *= rhs.m_Y;
		m_Z *= rhs.m_Z;
		m_W *= rhs.m_W;
		return *this;
	}

	Vector4& Vector4::operator*=(float scalar) noexcept
	{
		m_X *= scalar;
		m_Y *= scalar;
		m_Z *= scalar;
		m_W *= scalar;
		return *this;
	}

	Vector4& Vector4::operator/=(float scalar) noexcept
	{
		m_X /= scalar;
		m_Y /= scalar;
		m_Z /= scalar;
		m_W /= scalar;
		return *this;
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
