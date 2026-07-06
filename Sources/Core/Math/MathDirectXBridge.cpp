#include "Core/Precompiled.h"
#include "Core/Math/MathDirectXBridge.h"

#include <cfloat>
#include <cmath>
#include <type_traits>

namespace gglab::math::dx
{
	DirectX::XMVECTOR LoadVector2(const Vector2& value) noexcept
	{
		return DirectX::XMVectorSet(value.x, value.y, 0.0f, 0.0f);
	}

	DirectX::XMVECTOR LoadVector3(const Vector3& value) noexcept
	{
		return DirectX::XMVectorSet(value.x, value.y, value.z, 0.0f);
	}

	DirectX::XMVECTOR LoadVector4(const Vector4& value) noexcept
	{
		return DirectX::XMVectorSet(value.x, value.y, value.z, value.w);
	}

	DirectX::XMVECTOR LoadColor(const Color& value) noexcept
	{
		return DirectX::XMVectorSet(value.x, value.y, value.z, value.w);
	}

	DirectX::XMVECTOR LoadQuaternion(const Quaternion& value) noexcept
	{
		return DirectX::XMVectorSet(value.x, value.y, value.z, value.w);
	}

	DirectX::XMMATRIX LoadMatrix(const Matrix& value) noexcept
	{
		return DirectX::XMMatrixSet(
			value._11, value._12, value._13, value._14,
			value._21, value._22, value._23, value._24,
			value._31, value._32, value._33, value._34,
			value._41, value._42, value._43, value._44);
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

	Color StoreColor(DirectX::FXMVECTOR value) noexcept
	{
		return Color(
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

namespace gglab::math
{
	namespace
	{
		static_assert(std::is_standard_layout_v<Vector3>);
		static_assert(sizeof(Vector3) == sizeof(DirectX::XMFLOAT3));
		static_assert(offsetof(Vector3, x) == offsetof(DirectX::XMFLOAT3, x));
		static_assert(offsetof(Vector3, y) == offsetof(DirectX::XMFLOAT3, y));
		static_assert(offsetof(Vector3, z) == offsetof(DirectX::XMFLOAT3, z));

		DirectX::XMFLOAT3 ToDXFloat3(const Vector3& value) noexcept
		{
			return DirectX::XMFLOAT3(value.x, value.y, value.z);
		}

		DirectX::BoundingBox ToDXBoundingBox(const BoundingBox& value) noexcept
		{
			return DirectX::BoundingBox(
				ToDXFloat3(value.Center),
				ToDXFloat3(value.Extents));
		}

		DirectX::BoundingSphere ToDXBoundingSphere(const BoundingSphere& value) noexcept
		{
			return DirectX::BoundingSphere(
				ToDXFloat3(value.Center),
				value.Radius);
		}

		BoundingBox StoreBoundingBox(const DirectX::BoundingBox& value) noexcept
		{
			return BoundingBox(
				Vector3(value.Center.x, value.Center.y, value.Center.z),
				Vector3(value.Extents.x, value.Extents.y, value.Extents.z));
		}

		BoundingSphere StoreBoundingSphere(const DirectX::BoundingSphere& value) noexcept
		{
			return BoundingSphere(
				Vector3(value.Center.x, value.Center.y, value.Center.z),
				value.Radius);
		}
	}

	float Vector2::Length() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector2Length(dx::LoadVector2(*this)));
	}

	float Vector2::LengthSquared() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector2LengthSq(dx::LoadVector2(*this)));
	}

	float Vector2::Dot(const Vector2& rhs) const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector2Dot(dx::LoadVector2(*this), dx::LoadVector2(rhs)));
	}

	void Vector2::Normalize() noexcept
	{
		*this = dx::StoreVector2(DirectX::XMVector2Normalize(dx::LoadVector2(*this)));
	}

	void Vector2::Normalize(Vector2& result) const noexcept
	{
		result = dx::StoreVector2(DirectX::XMVector2Normalize(dx::LoadVector2(*this)));
	}

	Vector2 Vector2::Min(const Vector2& lhs, const Vector2& rhs) noexcept
	{
		return dx::StoreVector2(DirectX::XMVectorMin(dx::LoadVector2(lhs), dx::LoadVector2(rhs)));
	}

	Vector2 Vector2::Max(const Vector2& lhs, const Vector2& rhs) noexcept
	{
		return dx::StoreVector2(DirectX::XMVectorMax(dx::LoadVector2(lhs), dx::LoadVector2(rhs)));
	}

	Vector2 Vector2::Lerp(const Vector2& lhs, const Vector2& rhs, float t) noexcept
	{
		return dx::StoreVector2(DirectX::XMVectorLerp(dx::LoadVector2(lhs), dx::LoadVector2(rhs), t));
	}

	float Vector3::Length() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector3Length(dx::LoadVector3(*this)));
	}

	float Vector3::LengthSquared() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(dx::LoadVector3(*this)));
	}

	float Vector3::Dot(const Vector3& rhs) const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector3Dot(dx::LoadVector3(*this), dx::LoadVector3(rhs)));
	}

	Vector3 Vector3::Cross(const Vector3& rhs) const noexcept
	{
		return dx::StoreVector3(DirectX::XMVector3Cross(dx::LoadVector3(*this), dx::LoadVector3(rhs)));
	}

	void Vector3::Normalize() noexcept
	{
		*this = dx::StoreVector3(DirectX::XMVector3Normalize(dx::LoadVector3(*this)));
	}

	void Vector3::Normalize(Vector3& result) const noexcept
	{
		result = dx::StoreVector3(DirectX::XMVector3Normalize(dx::LoadVector3(*this)));
	}

	Vector3 Vector3::Normalized() const noexcept
	{
		return dx::StoreVector3(DirectX::XMVector3Normalize(dx::LoadVector3(*this)));
	}

	Vector3 Vector3::Min(const Vector3& lhs, const Vector3& rhs) noexcept
	{
		return dx::StoreVector3(DirectX::XMVectorMin(dx::LoadVector3(lhs), dx::LoadVector3(rhs)));
	}

	Vector3 Vector3::Max(const Vector3& lhs, const Vector3& rhs) noexcept
	{
		return dx::StoreVector3(DirectX::XMVectorMax(dx::LoadVector3(lhs), dx::LoadVector3(rhs)));
	}

	Vector3 Vector3::Lerp(const Vector3& lhs, const Vector3& rhs, float t) noexcept
	{
		return dx::StoreVector3(DirectX::XMVectorLerp(dx::LoadVector3(lhs), dx::LoadVector3(rhs), t));
	}

	Vector3 Vector3::Transform(const Vector3& value, const Matrix& matrix) noexcept
	{
		return dx::StoreVector3(DirectX::XMVector3TransformCoord(
			dx::LoadVector3(value),
			dx::LoadMatrix(matrix)));
	}

	void Vector3::Transform(const Vector3& value, const Matrix& matrix, Vector4& result) noexcept
	{
		result = dx::StoreVector4(DirectX::XMVector3Transform(
			dx::LoadVector3(value),
			dx::LoadMatrix(matrix)));
	}

	Vector3 Vector3::TransformNormal(const Vector3& value, const Matrix& matrix) noexcept
	{
		return dx::StoreVector3(DirectX::XMVector3TransformNormal(
			dx::LoadVector3(value),
			dx::LoadMatrix(matrix)));
	}

	float Vector4::Length() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector4Length(dx::LoadVector4(*this)));
	}

	float Vector4::LengthSquared() const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector4LengthSq(dx::LoadVector4(*this)));
	}

	float Vector4::Dot(const Vector4& rhs) const noexcept
	{
		return DirectX::XMVectorGetX(DirectX::XMVector4Dot(dx::LoadVector4(*this), dx::LoadVector4(rhs)));
	}

	void Vector4::Normalize() noexcept
	{
		*this = dx::StoreVector4(DirectX::XMVector4Normalize(dx::LoadVector4(*this)));
	}

	void Vector4::Normalize(Vector4& result) const noexcept
	{
		result = dx::StoreVector4(DirectX::XMVector4Normalize(dx::LoadVector4(*this)));
	}

	Vector4 Vector4::Min(const Vector4& lhs, const Vector4& rhs) noexcept
	{
		return dx::StoreVector4(DirectX::XMVectorMin(dx::LoadVector4(lhs), dx::LoadVector4(rhs)));
	}

	Vector4 Vector4::Max(const Vector4& lhs, const Vector4& rhs) noexcept
	{
		return dx::StoreVector4(DirectX::XMVectorMax(dx::LoadVector4(lhs), dx::LoadVector4(rhs)));
	}

	Vector4 Vector4::Lerp(const Vector4& lhs, const Vector4& rhs, float t) noexcept
	{
		return dx::StoreVector4(DirectX::XMVectorLerp(dx::LoadVector4(lhs), dx::LoadVector4(rhs), t));
	}

	Vector4 Vector4::Transform(const Vector4& value, const Matrix& matrix) noexcept
	{
		return dx::StoreVector4(DirectX::XMVector4Transform(
			dx::LoadVector4(value),
			dx::LoadMatrix(matrix)));
	}

	Matrix& Matrix::operator*=(const Matrix& rhs) noexcept
	{
		*this = *this * rhs;
		return *this;
	}

	Matrix Matrix::Invert() const noexcept
	{
		Matrix result;
		Invert(result);
		return result;
	}

	void Matrix::Invert(Matrix& result) const noexcept
	{
		DirectX::XMVECTOR determinant;
		result = dx::StoreMatrix(DirectX::XMMatrixInverse(&determinant, dx::LoadMatrix(*this)));
	}

	Matrix Matrix::Transpose() const noexcept
	{
		Matrix result;
		Transpose(result);
		return result;
	}

	void Matrix::Transpose(Matrix& result) const noexcept
	{
		result = dx::StoreMatrix(DirectX::XMMatrixTranspose(dx::LoadMatrix(*this)));
	}

	Matrix Matrix::CreateScale(const Vector3& scale) noexcept
	{
		return dx::StoreMatrix(DirectX::XMMatrixScaling(scale.x, scale.y, scale.z));
	}

	Matrix Matrix::CreateScale(float scale) noexcept
	{
		return dx::StoreMatrix(DirectX::XMMatrixScaling(scale, scale, scale));
	}

	Matrix Matrix::CreateTranslation(const Vector3& translation) noexcept
	{
		return dx::StoreMatrix(DirectX::XMMatrixTranslation(translation.x, translation.y, translation.z));
	}

	Matrix Matrix::CreateFromQuaternion(const Quaternion& rotation) noexcept
	{
		return dx::StoreMatrix(DirectX::XMMatrixRotationQuaternion(dx::LoadQuaternion(rotation)));
	}

	Matrix Matrix::CreateLookAt(const Vector3& eye, const Vector3& target, const Vector3& up) noexcept
	{
		return dx::StoreMatrix(DirectX::XMMatrixLookAtLH(
			dx::LoadVector3(eye),
			dx::LoadVector3(target),
			dx::LoadVector3(up)));
	}

	Matrix Matrix::CreatePerspectiveFieldOfView(float fovRadians, float aspect, float nearZ, float farZ) noexcept
	{
		return dx::StoreMatrix(DirectX::XMMatrixPerspectiveFovLH(fovRadians, aspect, nearZ, farZ));
	}

	Matrix Matrix::CreateOrthographicOffCenter(float left, float right, float bottom, float top, float nearZ, float farZ) noexcept
	{
		return dx::StoreMatrix(DirectX::XMMatrixOrthographicOffCenterLH(left, right, bottom, top, nearZ, farZ));
	}

	Quaternion& Quaternion::operator*=(const Quaternion& rhs) noexcept
	{
		*this = *this * rhs;
		return *this;
	}

	Vector3 Quaternion::ToEuler() const noexcept
	{
		const float xx = x * x;
		const float yy = y * y;
		const float zz = z * z;

		const float m31 = 2.0f * x * z + 2.0f * y * w;
		const float m32 = 2.0f * y * z - 2.0f * x * w;
		const float m33 = 1.0f - 2.0f * xx - 2.0f * yy;

		const float cy = std::sqrt(m33 * m33 + m31 * m31);
		const float cx = std::atan2(-m32, cy);
		if (cy > 16.0f * FLT_EPSILON)
		{
			const float m12 = 2.0f * x * y + 2.0f * z * w;
			const float m22 = 1.0f - 2.0f * xx - 2.0f * zz;
			return Vector3(cx, std::atan2(m31, m33), std::atan2(m12, m22));
		}

		const float m11 = 1.0f - 2.0f * yy - 2.0f * zz;
		const float m21 = 2.0f * x * y - 2.0f * z * w;
		return Vector3(cx, 0.0f, std::atan2(-m21, m11));
	}

	void Quaternion::Normalize() noexcept
	{
		*this = dx::StoreQuaternion(DirectX::XMQuaternionNormalize(dx::LoadQuaternion(*this)));
	}

	void Quaternion::Normalize(Quaternion& result) const noexcept
	{
		result = dx::StoreQuaternion(DirectX::XMQuaternionNormalize(dx::LoadQuaternion(*this)));
	}

	Quaternion Quaternion::CreateFromYawPitchRoll(float yaw, float pitch, float roll) noexcept
	{
		return dx::StoreQuaternion(DirectX::XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
	}

	Quaternion Quaternion::CreateFromYawPitchRoll(const Vector3& angles) noexcept
	{
		return dx::StoreQuaternion(DirectX::XMQuaternionRotationRollPitchYawFromVector(dx::LoadVector3(angles)));
	}

	void Quaternion::FromToRotation(const Vector3& fromDir, const Vector3& toDir, Quaternion& result) noexcept
	{
		const DirectX::XMVECTOR from = DirectX::XMVector3Normalize(dx::LoadVector3(fromDir));
		const DirectX::XMVECTOR to = DirectX::XMVector3Normalize(dx::LoadVector3(toDir));

		const float dot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(from, to));
		if (dot >= 1.0f)
		{
			result = Identity;
			return;
		}
		if (dot <= -1.0f)
		{
			DirectX::XMVECTOR axis = DirectX::XMVector3Cross(from, dx::LoadVector3(Vector3::Right));
			if (DirectX::XMVector3NearEqual(DirectX::XMVector3LengthSq(axis), DirectX::g_XMZero, DirectX::g_XMEpsilon))
			{
				axis = DirectX::XMVector3Cross(from, dx::LoadVector3(Vector3::Up));
			}
			result = dx::StoreQuaternion(DirectX::XMQuaternionRotationAxis(axis, Pi));
			return;
		}

		const DirectX::XMVECTOR cross = DirectX::XMVector3Cross(from, to);
		result = dx::StoreQuaternion(cross);
		const float scale = std::sqrt((1.0f + dot) * 2.0f);
		result.x /= scale;
		result.y /= scale;
		result.z /= scale;
		result.w = scale * 0.5f;
	}

	Quaternion Quaternion::FromToRotation(const Vector3& fromDir, const Vector3& toDir) noexcept
	{
		Quaternion result;
		FromToRotation(fromDir, toDir, result);
		return result;
	}

	void BoundingBox::Transform(BoundingBox& result, const Matrix& matrix) const noexcept
	{
		DirectX::BoundingBox dxResult;
		ToDXBoundingBox(*this).Transform(dxResult, dx::LoadMatrix(matrix));
		result = StoreBoundingBox(dxResult);
	}

	void BoundingBox::CreateFromPoints(
		BoundingBox& result,
		size_t count,
		const Vector3* points,
		size_t stride) noexcept
	{
		if (points == nullptr || count == 0)
		{
			result = BoundingBox{};
			return;
		}

		DirectX::BoundingBox dxResult;
		DirectX::BoundingBox::CreateFromPoints(
			dxResult,
			count,
			reinterpret_cast<const DirectX::XMFLOAT3*>(points),
			stride);
		result = StoreBoundingBox(dxResult);
	}

	void BoundingBox::CreateMerged(
		BoundingBox& result,
		const BoundingBox& lhs,
		const BoundingBox& rhs) noexcept
	{
		DirectX::BoundingBox dxResult;
		DirectX::BoundingBox::CreateMerged(
			dxResult,
			ToDXBoundingBox(lhs),
			ToDXBoundingBox(rhs));
		result = StoreBoundingBox(dxResult);
	}

	void BoundingSphere::Transform(BoundingSphere& result, const Matrix& matrix) const noexcept
	{
		DirectX::BoundingSphere dxResult;
		ToDXBoundingSphere(*this).Transform(dxResult, dx::LoadMatrix(matrix));
		result = StoreBoundingSphere(dxResult);
	}

	void BoundingSphere::CreateFromPoints(
		BoundingSphere& result,
		size_t count,
		const Vector3* points,
		size_t stride) noexcept
	{
		if (points == nullptr || count == 0)
		{
			result = BoundingSphere{};
			return;
		}

		DirectX::BoundingSphere dxResult;
		DirectX::BoundingSphere::CreateFromPoints(
			dxResult,
			count,
			reinterpret_cast<const DirectX::XMFLOAT3*>(points),
			stride);
		result = StoreBoundingSphere(dxResult);
	}

	void BoundingSphere::CreateFromBoundingBox(BoundingSphere& result, const BoundingBox& box) noexcept
	{
		DirectX::BoundingSphere dxResult;
		DirectX::BoundingSphere::CreateFromBoundingBox(dxResult, ToDXBoundingBox(box));
		result = StoreBoundingSphere(dxResult);
	}

	Matrix operator*(const Matrix& lhs, const Matrix& rhs) noexcept
	{
		return dx::StoreMatrix(DirectX::XMMatrixMultiply(dx::LoadMatrix(lhs), dx::LoadMatrix(rhs)));
	}

	Quaternion operator*(const Quaternion& lhs, const Quaternion& rhs) noexcept
	{
		return dx::StoreQuaternion(DirectX::XMQuaternionMultiply(
			dx::LoadQuaternion(lhs),
			dx::LoadQuaternion(rhs)));
	}
}
