#include "Core/Precompiled.h"
#include "Core/Math/MathTypes.h"

#include <DirectXCollision.h>
#include <DirectXMath.h>

#include <type_traits>

namespace gglab::math
{
	namespace
	{
		static_assert(std::is_standard_layout_v<Vector3>);
		static_assert(sizeof(Vector3) == sizeof(DirectX::XMFLOAT3));
		static_assert(offsetof(Vector3, m_X) == offsetof(DirectX::XMFLOAT3, x));
		static_assert(offsetof(Vector3, m_Y) == offsetof(DirectX::XMFLOAT3, y));
		static_assert(offsetof(Vector3, m_Z) == offsetof(DirectX::XMFLOAT3, z));

		DirectX::XMFLOAT3 ToDXFloat3(const Vector3& value) noexcept
		{
			return DirectX::XMFLOAT3(value.m_X, value.m_Y, value.m_Z);
		}

		DirectX::XMMATRIX LoadMatrix(const Matrix& value) noexcept
		{
			return DirectX::XMMatrixSet(
				value.m_11, value.m_12, value.m_13, value.m_14,
				value.m_21, value.m_22, value.m_23, value.m_24,
				value.m_31, value.m_32, value.m_33, value.m_34,
				value.m_41, value.m_42, value.m_43, value.m_44);
		}

		DirectX::BoundingBox ToDXBoundingBox(const BoundingBox& value) noexcept
		{
			return DirectX::BoundingBox(
				ToDXFloat3(value.m_Center),
				ToDXFloat3(value.m_Extents));
		}

		DirectX::BoundingSphere ToDXBoundingSphere(const BoundingSphere& value) noexcept
		{
			return DirectX::BoundingSphere(
				ToDXFloat3(value.m_Center),
				value.m_Radius);
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

	void BoundingBox::Transform(BoundingBox& result, const Matrix& matrix) const noexcept
	{
		DirectX::BoundingBox dxResult;
		ToDXBoundingBox(*this).Transform(dxResult, LoadMatrix(matrix));
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
		ToDXBoundingSphere(*this).Transform(dxResult, LoadMatrix(matrix));
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
}
