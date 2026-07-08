#include "Core/Precompiled.h"
#include "Core/Math/MathConstants.h"
#include "Core/Math/MathFunctions.h"
#include "Core/Math/Matrix.h"
#include "Core/Math/Quaternion.h"

#include <DirectXMath.h>

#include <cfloat>
#include <cmath>

namespace gglab::math
{
	namespace
	{
		DirectX::XMVECTOR LoadVector2(const Vector2& value) noexcept
		{
			return DirectX::XMVectorSet(value.m_X, value.m_Y, 0.0f, 0.0f);
		}

		DirectX::XMVECTOR LoadVector3(const Vector3& value) noexcept
		{
			return DirectX::XMVectorSet(value.m_X, value.m_Y, value.m_Z, 0.0f);
		}

		DirectX::XMVECTOR LoadVector4(const Vector4& value) noexcept
		{
			return DirectX::XMVectorSet(value.m_X, value.m_Y, value.m_Z, value.m_W);
		}

		DirectX::XMVECTOR LoadQuaternion(const Quaternion& value) noexcept
		{
			return DirectX::XMVectorSet(value.m_X, value.m_Y, value.m_Z, value.m_W);
		}

		DirectX::XMMATRIX LoadMatrix(const Matrix& value) noexcept
		{
			return DirectX::XMMatrixSet(
				value.m_11, value.m_12, value.m_13, value.m_14,
				value.m_21, value.m_22, value.m_23, value.m_24,
				value.m_31, value.m_32, value.m_33, value.m_34,
				value.m_41, value.m_42, value.m_43, value.m_44);
		}

		Vector2 StoreVector2(DirectX::FXMVECTOR value) noexcept
		{
			return Vector2(
				DirectX::XMVectorGetX(value),
				DirectX::XMVectorGetY(value));
		}

		Vector3 StoreVector3(DirectX::FXMVECTOR value) noexcept
		{
			return Vector3(
				DirectX::XMVectorGetX(value),
				DirectX::XMVectorGetY(value),
				DirectX::XMVectorGetZ(value));
		}

		Vector4 StoreVector4(DirectX::FXMVECTOR value) noexcept
		{
			return Vector4(
				DirectX::XMVectorGetX(value),
				DirectX::XMVectorGetY(value),
				DirectX::XMVectorGetZ(value),
				DirectX::XMVectorGetW(value));
		}

		Quaternion StoreQuaternion(DirectX::FXMVECTOR value) noexcept
		{
			return Quaternion(
				DirectX::XMVectorGetX(value),
				DirectX::XMVectorGetY(value),
				DirectX::XMVectorGetZ(value),
				DirectX::XMVectorGetW(value));
		}

		Matrix StoreMatrix(DirectX::FXMMATRIX value) noexcept
		{
			DirectX::XMFLOAT4X4 stored;
			DirectX::XMStoreFloat4x4(&stored, value);
			return Matrix(
				stored._11, stored._12, stored._13, stored._14,
				stored._21, stored._22, stored._23, stored._24,
				stored._31, stored._32, stored._33, stored._34,
				stored._41, stored._42, stored._43, stored._44);
		}
	}

	float Vector2::Length() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector2Length(LoadVector2(*this)));
	}

	float Vector2::LengthSquared() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector2LengthSq(LoadVector2(*this)));
	}

	float Vector2::Dot(const Vector2& rhs) const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector2Dot(LoadVector2(*this), LoadVector2(rhs)));
	}

	void Vector2::Normalize() noexcept
	{
		*this = StoreVector2(DirectX::XMVector2Normalize(LoadVector2(*this)));
	}

	void Vector2::Normalize(Vector2& result) const noexcept
	{
		result = StoreVector2(DirectX::XMVector2Normalize(LoadVector2(*this)));
	}

	Vector2 Min(const Vector2& lhs, const Vector2& rhs) noexcept
	{
		return StoreVector2(DirectX::XMVectorMin(LoadVector2(lhs), LoadVector2(rhs)));
	}

	Vector2 Max(const Vector2& lhs, const Vector2& rhs) noexcept
	{
		return StoreVector2(DirectX::XMVectorMax(LoadVector2(lhs), LoadVector2(rhs)));
	}

	Vector2 Lerp(const Vector2& lhs, const Vector2& rhs, float t) noexcept
	{
		return StoreVector2(DirectX::XMVectorLerp(LoadVector2(lhs), LoadVector2(rhs), t));
	}

	bool TryNormalize(const Vector2& value, Vector2& result, float tolerance) noexcept
	{
		result = Vector2::Zero;
		if (!IsFinite(value) || !std::isfinite(tolerance) || tolerance < 0.0f ||
			value.LengthSquared() <= tolerance * tolerance)
		{
			return false;
		}
		value.Normalize(result);
		return IsFinite(result);
	}

	Vector2 NormalizeOr(const Vector2& value, const Vector2& fallback, float tolerance) noexcept
	{
		Vector2 result{};
		return TryNormalize(value, result, tolerance) ? result : fallback;
	}

	float Vector3::Length() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector3Length(LoadVector3(*this)));
	}

	float Vector3::LengthSquared() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(LoadVector3(*this)));
	}

	float Vector3::Dot(const Vector3& rhs) const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector3Dot(LoadVector3(*this), LoadVector3(rhs)));
	}

	Vector3 Vector3::Cross(const Vector3& rhs) const noexcept
	{
		return StoreVector3(DirectX::XMVector3Cross(LoadVector3(*this), LoadVector3(rhs)));
	}

	void Vector3::Normalize() noexcept
	{
		*this = StoreVector3(DirectX::XMVector3Normalize(LoadVector3(*this)));
	}

	void Vector3::Normalize(Vector3& result) const noexcept
	{
		result = StoreVector3(DirectX::XMVector3Normalize(LoadVector3(*this)));
	}

	Vector3 Vector3::Normalized() const noexcept
	{
		return StoreVector3(DirectX::XMVector3Normalize(LoadVector3(*this)));
	}

	Vector3 Min(const Vector3& lhs, const Vector3& rhs) noexcept
	{
		return StoreVector3(DirectX::XMVectorMin(LoadVector3(lhs), LoadVector3(rhs)));
	}

	Vector3 Max(const Vector3& lhs, const Vector3& rhs) noexcept
	{
		return StoreVector3(DirectX::XMVectorMax(LoadVector3(lhs), LoadVector3(rhs)));
	}

	Vector3 Lerp(const Vector3& lhs, const Vector3& rhs, float t) noexcept
	{
		return StoreVector3(DirectX::XMVectorLerp(LoadVector3(lhs), LoadVector3(rhs), t));
	}

	Vector3 TransformPoint(const Vector3& value, const Matrix& matrix) noexcept
	{
		return StoreVector3(DirectX::XMVector3TransformCoord(
			LoadVector3(value),
			LoadMatrix(matrix)));
	}

	Vector4 TransformPointHomogeneous(const Vector3& value, const Matrix& matrix) noexcept
	{
		return StoreVector4(DirectX::XMVector3Transform(
			LoadVector3(value),
			LoadMatrix(matrix)));
	}

	Vector3 TransformDirection(const Vector3& value, const Matrix& matrix) noexcept
	{
		return StoreVector3(DirectX::XMVector3TransformNormal(
			LoadVector3(value),
			LoadMatrix(matrix)));
	}

	bool TryNormalize(const Vector3& value, Vector3& result, float tolerance) noexcept
	{
		result = Vector3::Zero;
		if (!IsFinite(value) || !std::isfinite(tolerance) || tolerance < 0.0f ||
			value.LengthSquared() <= tolerance * tolerance)
		{
			return false;
		}
		result = value.Normalized();
		return IsFinite(result);
	}

	Vector3 NormalizeOr(const Vector3& value, const Vector3& fallback, float tolerance) noexcept
	{
		Vector3 result{};
		return TryNormalize(value, result, tolerance) ? result : fallback;
	}

	float Vector4::Length() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector4Length(LoadVector4(*this)));
	}

	float Vector4::LengthSquared() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector4LengthSq(LoadVector4(*this)));
	}

	float Vector4::Dot(const Vector4& rhs) const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector4Dot(LoadVector4(*this), LoadVector4(rhs)));
	}

	void Vector4::Normalize() noexcept
	{
		*this = StoreVector4(DirectX::XMVector4Normalize(LoadVector4(*this)));
	}

	void Vector4::Normalize(Vector4& result) const noexcept
	{
		result = StoreVector4(DirectX::XMVector4Normalize(LoadVector4(*this)));
	}

	Vector4 Min(const Vector4& lhs, const Vector4& rhs) noexcept
	{
		return StoreVector4(DirectX::XMVectorMin(LoadVector4(lhs), LoadVector4(rhs)));
	}

	Vector4 Max(const Vector4& lhs, const Vector4& rhs) noexcept
	{
		return StoreVector4(DirectX::XMVectorMax(LoadVector4(lhs), LoadVector4(rhs)));
	}

	Vector4 Lerp(const Vector4& lhs, const Vector4& rhs, float t) noexcept
	{
		return StoreVector4(DirectX::XMVectorLerp(LoadVector4(lhs), LoadVector4(rhs), t));
	}

	Vector4 Transform(const Vector4& value, const Matrix& matrix) noexcept
	{
		return StoreVector4(DirectX::XMVector4Transform(
			LoadVector4(value),
			LoadMatrix(matrix)));
	}

	bool TryNormalize(const Vector4& value, Vector4& result, float tolerance) noexcept
	{
		result = Vector4::Zero;
		if (!IsFinite(value) || !std::isfinite(tolerance) || tolerance < 0.0f ||
			value.LengthSquared() <= tolerance * tolerance)
		{
			return false;
		}
		value.Normalize(result);
		return IsFinite(result);
	}

	Vector4 NormalizeOr(const Vector4& value, const Vector4& fallback, float tolerance) noexcept
	{
		Vector4 result{};
		return TryNormalize(value, result, tolerance) ? result : fallback;
	}

	Matrix& Matrix::operator*=(const Matrix& rhs) noexcept
	{
		*this = *this * rhs;
		return *this;
	}

	Matrix Inverse(const Matrix& matrix) noexcept
	{
		DirectX::XMVECTOR determinant;
		return StoreMatrix(DirectX::XMMatrixInverse(&determinant, LoadMatrix(matrix)));
	}

	bool TryInverse(const Matrix& matrix, Matrix& result, float determinantTolerance) noexcept
	{
		result = Matrix::Identity;
		if (!IsFinite(matrix) || !std::isfinite(determinantTolerance) || determinantTolerance < 0.0f)
		{
			return false;
		}

		DirectX::XMVECTOR determinant;
		const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(&determinant, LoadMatrix(matrix));
		const float determinantValue = DirectX::XMVectorGetX(determinant);
		if (!std::isfinite(determinantValue) || std::abs(determinantValue) <= determinantTolerance)
		{
			return false;
		}

		result = StoreMatrix(inverse);
		return IsFinite(result);
	}

	Matrix SafeInverse(
		const Matrix& matrix,
		const Matrix& fallback,
		float determinantTolerance) noexcept
	{
		Matrix result{};
		return TryInverse(matrix, result, determinantTolerance) ? result : fallback;
	}

	Matrix SafeInverse(const Matrix& matrix, float determinantTolerance) noexcept
	{
		return SafeInverse(matrix, Matrix::Identity, determinantTolerance);
	}

	Matrix Transpose(const Matrix& matrix) noexcept
	{
		return StoreMatrix(DirectX::XMMatrixTranspose(LoadMatrix(matrix)));
	}

	Matrix CreateScale(const Vector3& scale) noexcept
	{
		return StoreMatrix(DirectX::XMMatrixScaling(scale.m_X, scale.m_Y, scale.m_Z));
	}

	Matrix CreateScale(float scale) noexcept
	{
		return StoreMatrix(DirectX::XMMatrixScaling(scale, scale, scale));
	}

	Matrix CreateTranslation(const Vector3& translation) noexcept
	{
		return StoreMatrix(DirectX::XMMatrixTranslation(translation.m_X, translation.m_Y, translation.m_Z));
	}

	Matrix CreateFromQuaternion(const Quaternion& rotation) noexcept
	{
		return StoreMatrix(DirectX::XMMatrixRotationQuaternion(LoadQuaternion(rotation)));
	}

	Matrix CreateLookAtLH(const Vector3& eye, const Vector3& target, const Vector3& up) noexcept
	{
		return StoreMatrix(DirectX::XMMatrixLookAtLH(
			LoadVector3(eye),
			LoadVector3(target),
			LoadVector3(up)));
	}

	Matrix CreatePerspectiveFieldOfViewLH(float fovRadians, float aspect, float nearZ, float farZ) noexcept
	{
		return StoreMatrix(DirectX::XMMatrixPerspectiveFovLH(fovRadians, aspect, nearZ, farZ));
	}

	Matrix CreateOrthographicOffCenterLH(float left, float right, float bottom, float top, float nearZ, float farZ) noexcept
	{
		return StoreMatrix(DirectX::XMMatrixOrthographicOffCenterLH(left, right, bottom, top, nearZ, farZ));
	}

	Quaternion& Quaternion::operator*=(const Quaternion& rhs) noexcept
	{
		*this = *this * rhs;
		return *this;
	}

	Vector3 Quaternion::ToEuler() const noexcept
	{
		const float xx = m_X * m_X;
		const float yy = m_Y * m_Y;
		const float zz = m_Z * m_Z;

		const float m31 = 2.0f * m_X * m_Z + 2.0f * m_Y * m_W;
		const float m32 = 2.0f * m_Y * m_Z - 2.0f * m_X * m_W;
		const float m33 = 1.0f - 2.0f * xx - 2.0f * yy;

		const float cy = std::sqrt(m33 * m33 + m31 * m31);
		const float cx = std::atan2(-m32, cy);
		if (cy > 16.0f * FLT_EPSILON)
		{
			const float m12 = 2.0f * m_X * m_Y + 2.0f * m_Z * m_W;
			const float m22 = 1.0f - 2.0f * xx - 2.0f * zz;
			return Vector3(cx, std::atan2(m31, m33), std::atan2(m12, m22));
		}

		const float m11 = 1.0f - 2.0f * yy - 2.0f * zz;
		const float m21 = 2.0f * m_X * m_Y - 2.0f * m_Z * m_W;
		return Vector3(cx, 0.0f, std::atan2(-m21, m11));
	}

	void Quaternion::Normalize() noexcept
	{
		*this = StoreQuaternion(DirectX::XMQuaternionNormalize(LoadQuaternion(*this)));
	}

	void Quaternion::Normalize(Quaternion& result) const noexcept
	{
		result = StoreQuaternion(DirectX::XMQuaternionNormalize(LoadQuaternion(*this)));
	}

	Quaternion CreateFromYawPitchRoll(float yaw, float pitch, float roll) noexcept
	{
		return StoreQuaternion(DirectX::XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
	}

	Quaternion CreateFromYawPitchRoll(const Vector3& angles) noexcept
	{
		return StoreQuaternion(DirectX::XMQuaternionRotationRollPitchYawFromVector(LoadVector3(angles)));
	}

	bool TryNormalize(const Quaternion& value, Quaternion& result, float tolerance) noexcept
	{
		result = Quaternion::Identity;
		if (!IsFinite(value) || !std::isfinite(tolerance) || tolerance < 0.0f)
		{
			return false;
		}

		const float lengthSquared =
			value.m_X * value.m_X +
			value.m_Y * value.m_Y +
			value.m_Z * value.m_Z +
			value.m_W * value.m_W;
		if (lengthSquared <= tolerance * tolerance)
		{
			return false;
		}

		result = StoreQuaternion(DirectX::XMQuaternionNormalize(LoadQuaternion(value)));
		return IsFinite(result);
	}

	Quaternion NormalizeOr(const Quaternion& value, const Quaternion& fallback, float tolerance) noexcept
	{
		Quaternion result{};
		return TryNormalize(value, result, tolerance) ? result : fallback;
	}

	Quaternion RotationFromTo(const Vector3& fromDir, const Vector3& toDir) noexcept
	{
		const DirectX::XMVECTOR from = DirectX::XMVector3Normalize(LoadVector3(fromDir));
		const DirectX::XMVECTOR to = DirectX::XMVector3Normalize(LoadVector3(toDir));

		const float dot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(from, to));
		if (dot >= 1.0f)
		{
			return Quaternion::Identity;
		}
		if (dot <= -1.0f)
		{
			DirectX::XMVECTOR axis = DirectX::XMVector3Cross(from, LoadVector3(Vector3::Right));
			if (DirectX::XMVector3NearEqual(DirectX::XMVector3LengthSq(axis), DirectX::g_XMZero, DirectX::g_XMEpsilon))
			{
				axis = DirectX::XMVector3Cross(from, LoadVector3(Vector3::Up));
			}
			return StoreQuaternion(DirectX::XMQuaternionRotationAxis(axis, Pi));
		}

		const DirectX::XMVECTOR cross = DirectX::XMVector3Cross(from, to);
		Quaternion result = StoreQuaternion(cross);
		const float scale = std::sqrt((1.0f + dot) * 2.0f);
		result.m_X /= scale;
		result.m_Y /= scale;
		result.m_Z /= scale;
		result.m_W = scale * 0.5f;
		return result;
	}

	bool TryRotationFromTo(
		const Vector3& fromDir,
		const Vector3& toDir,
		Quaternion& result,
		float tolerance) noexcept
	{
		result = Quaternion::Identity;
		Vector3 normalizedFrom{};
		Vector3 normalizedTo{};
		if (!TryNormalize(fromDir, normalizedFrom, tolerance) ||
			!TryNormalize(toDir, normalizedTo, tolerance))
		{
			return false;
		}
		result = RotationFromTo(normalizedFrom, normalizedTo);
		return IsFinite(result);
	}

	Matrix operator*(const Matrix& lhs, const Matrix& rhs) noexcept
	{
		return StoreMatrix(DirectX::XMMatrixMultiply(LoadMatrix(lhs), LoadMatrix(rhs)));
	}

	Quaternion Slerp(const Quaternion& lhs, const Quaternion& rhs, float t) noexcept
	{
		return StoreQuaternion(DirectX::XMQuaternionSlerp(
			LoadQuaternion(lhs),
			LoadQuaternion(rhs),
			t));
	}

	Quaternion operator*(const Quaternion& lhs, const Quaternion& rhs) noexcept
	{
		return StoreQuaternion(DirectX::XMQuaternionMultiply(
			LoadQuaternion(lhs),
			LoadQuaternion(rhs)));
	}
}
