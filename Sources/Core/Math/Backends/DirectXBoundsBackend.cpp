#include "Core/Precompiled.h"
#include "Core/Math/BoundingVolumes.h"

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

		DirectX::BoundingBox ToDXBoundingBox(const Aabb& value) noexcept
		{
			return DirectX::BoundingBox(
				ToDXFloat3(value.m_Center),
				ToDXFloat3(value.m_Extents));
		}

		DirectX::BoundingSphere ToDXBoundingSphere(const Sphere& value) noexcept
		{
			return DirectX::BoundingSphere(
				ToDXFloat3(value.m_Center),
				value.m_Radius);
		}

		Aabb StoreAabb(const DirectX::BoundingBox& value) noexcept
		{
			return Aabb(
				Vector3(value.Center.x, value.Center.y, value.Center.z),
				Vector3(value.Extents.x, value.Extents.y, value.Extents.z));
		}

		Sphere StoreSphere(const DirectX::BoundingSphere& value) noexcept
		{
			return Sphere(
				Vector3(value.Center.x, value.Center.y, value.Center.z),
				value.Radius);
		}
	}

	Aabb Transform(const Aabb& aabb, const Matrix& matrix) noexcept
	{
		DirectX::BoundingBox dxResult;
		ToDXBoundingBox(aabb).Transform(dxResult, LoadMatrix(matrix));
		return StoreAabb(dxResult);
	}

	Aabb CreateAabbFromPoints(
		size_t count,
		const Vector3* points,
		size_t stride) noexcept
	{
		if (points == nullptr || count == 0)
		{
			return {};
		}

		DirectX::BoundingBox dxResult;
		DirectX::BoundingBox::CreateFromPoints(
			dxResult,
			count,
			reinterpret_cast<const DirectX::XMFLOAT3*>(points),
			stride);
		return StoreAabb(dxResult);
	}

	Aabb Merge(const Aabb& lhs,
		const Aabb& rhs) noexcept
	{
		DirectX::BoundingBox dxResult;
		DirectX::BoundingBox::CreateMerged(
			dxResult,
			ToDXBoundingBox(lhs),
			ToDXBoundingBox(rhs));
		return StoreAabb(dxResult);
	}

	Sphere Transform(const Sphere& sphere, const Matrix& matrix) noexcept
	{
		DirectX::BoundingSphere dxResult;
		ToDXBoundingSphere(sphere).Transform(dxResult, LoadMatrix(matrix));
		return StoreSphere(dxResult);
	}

	Sphere CreateSphereFromPoints(
		size_t count,
		const Vector3* points,
		size_t stride) noexcept
	{
		if (points == nullptr || count == 0)
		{
			return {};
		}

		DirectX::BoundingSphere dxResult;
		DirectX::BoundingSphere::CreateFromPoints(
			dxResult,
			count,
			reinterpret_cast<const DirectX::XMFLOAT3*>(points),
			stride);
		return StoreSphere(dxResult);
	}

	Sphere CreateSphere(const Aabb& aabb) noexcept
	{
		DirectX::BoundingSphere dxResult;
		DirectX::BoundingSphere::CreateFromBoundingBox(dxResult, ToDXBoundingBox(aabb));
		return StoreSphere(dxResult);
	}
}
