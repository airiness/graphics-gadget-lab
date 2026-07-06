#pragma once
#include "Core/Math/Matrix.h"

namespace gglab::math
{
	struct BoundingBox
	{
		constexpr BoundingBox() noexcept = default;
		constexpr BoundingBox(const Vector3& center, const Vector3& extents) noexcept :
			m_Center(center),
			m_Extents(extents)
		{
		}

		void Transform(BoundingBox& result, const Matrix& matrix) const noexcept;

		static void CreateFromPoints(BoundingBox& result, size_t count, const Vector3* points, size_t stride) noexcept;
		static void CreateMerged(BoundingBox& result, const BoundingBox& lhs, const BoundingBox& rhs) noexcept;

		Vector3 m_Center = Vector3::Zero;
		Vector3 m_Extents = Vector3::Zero;
	};

	struct BoundingSphere
	{
		constexpr BoundingSphere() noexcept = default;
		constexpr BoundingSphere(const Vector3& center, float radius) noexcept :
			m_Center(center),
			m_Radius(radius)
		{
		}

		void Transform(BoundingSphere& result, const Matrix& matrix) const noexcept;

		static void CreateFromPoints(BoundingSphere& result, size_t count, const Vector3* points, size_t stride) noexcept;
		static void CreateFromBoundingBox(BoundingSphere& result, const BoundingBox& box) noexcept;

		Vector3 m_Center = Vector3::Zero;
		float m_Radius = 0.0f;
	};
}
