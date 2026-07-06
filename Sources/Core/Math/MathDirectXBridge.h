#pragma once
#include "Core/Math/MathTypes.h"

#include <DirectXCollision.h>
#include <DirectXMath.h>

namespace gglab::math::dx
{
	DirectX::XMVECTOR LoadVector2(const Vector2& value) noexcept;
	DirectX::XMVECTOR LoadVector3(const Vector3& value) noexcept;
	DirectX::XMVECTOR LoadVector4(const Vector4& value) noexcept;
	DirectX::XMVECTOR LoadColor(const Color& value) noexcept;
	DirectX::XMVECTOR LoadQuaternion(const Quaternion& value) noexcept;
	DirectX::XMMATRIX LoadMatrix(const Matrix& value) noexcept;

	Vector2 StoreVector2(DirectX::FXMVECTOR value) noexcept;
	Vector3 StoreVector3(DirectX::FXMVECTOR value) noexcept;
	Vector4 StoreVector4(DirectX::FXMVECTOR value) noexcept;
	Color StoreColor(DirectX::FXMVECTOR value) noexcept;
	Quaternion StoreQuaternion(DirectX::FXMVECTOR value) noexcept;
	Matrix StoreMatrix(DirectX::FXMMATRIX value) noexcept;
}
