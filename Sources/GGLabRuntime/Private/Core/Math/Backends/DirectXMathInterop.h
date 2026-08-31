#pragma once
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"

#include <DirectXMath.h>

namespace gglab::math::directx
{
	[[nodiscard]] inline DirectX::XMFLOAT3 ToDXFloat3(const Vector3& value) noexcept
	{
		return DirectX::XMFLOAT3(value.m_X, value.m_Y, value.m_Z);
	}

	[[nodiscard]] inline DirectX::XMVECTOR LoadVector2(const Vector2& value) noexcept
	{
		return DirectX::XMVectorSet(value.m_X, value.m_Y, 0.0f, 0.0f);
	}

	[[nodiscard]] inline DirectX::XMVECTOR LoadVector3(const Vector3& value) noexcept
	{
		return DirectX::XMVectorSet(value.m_X, value.m_Y, value.m_Z, 0.0f);
	}

	[[nodiscard]] inline DirectX::XMVECTOR LoadVector4(const Vector4& value) noexcept
	{
		return DirectX::XMVectorSet(value.m_X, value.m_Y, value.m_Z, value.m_W);
	}

	[[nodiscard]] inline DirectX::XMVECTOR LoadQuaternion(const Quaternion& value) noexcept
	{
		return DirectX::XMVectorSet(value.m_X, value.m_Y, value.m_Z, value.m_W);
	}

	[[nodiscard]] inline DirectX::XMMATRIX LoadMatrix(const Matrix& value) noexcept
	{
		return DirectX::XMMatrixSet(
			value.m_11, value.m_12, value.m_13, value.m_14,
			value.m_21, value.m_22, value.m_23, value.m_24,
			value.m_31, value.m_32, value.m_33, value.m_34,
			value.m_41, value.m_42, value.m_43, value.m_44);
	}

	[[nodiscard]] inline Vector2 StoreVector2(DirectX::FXMVECTOR value) noexcept
	{
		return Vector2(
			DirectX::XMVectorGetX(value),
			DirectX::XMVectorGetY(value));
	}

	[[nodiscard]] inline Vector3 StoreVector3(DirectX::FXMVECTOR value) noexcept
	{
		return Vector3(
			DirectX::XMVectorGetX(value),
			DirectX::XMVectorGetY(value),
			DirectX::XMVectorGetZ(value));
	}

	[[nodiscard]] inline Vector4 StoreVector4(DirectX::FXMVECTOR value) noexcept
	{
		return Vector4(
			DirectX::XMVectorGetX(value),
			DirectX::XMVectorGetY(value),
			DirectX::XMVectorGetZ(value),
			DirectX::XMVectorGetW(value));
	}

	[[nodiscard]] inline Quaternion StoreQuaternion(DirectX::FXMVECTOR value) noexcept
	{
		return Quaternion(
			DirectX::XMVectorGetX(value),
			DirectX::XMVectorGetY(value),
			DirectX::XMVectorGetZ(value),
			DirectX::XMVectorGetW(value));
	}

	[[nodiscard]] inline Matrix StoreMatrix(DirectX::FXMMATRIX value) noexcept
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
