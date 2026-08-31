#pragma once
#include "GGLabRuntime/Core/Math/Matrix.h"

namespace gglab::math
{
	struct Aabb
	{
		constexpr Aabb() noexcept = default;
		constexpr Aabb(const Vector3& center, const Vector3& extents) noexcept :
			m_Center(center), m_Extents(extents)
		{
		}

		Vector3 m_Center = Vector3::Zero;
		Vector3 m_Extents = Vector3::Zero;
	};

	struct Sphere
	{
		constexpr Sphere() noexcept = default;
		constexpr Sphere(const Vector3& center, float radius) noexcept :
			m_Center(center), m_Radius(radius)
		{
		}

		Vector3 m_Center = Vector3::Zero;
		float m_Radius = 0.0f;
	};

	Aabb Transform(const Aabb& aabb, const Matrix& matrix) noexcept;
	Aabb CreateAabbFromPoints(size_t count, const Vector3* points, size_t stride) noexcept;
	Aabb Merge(const Aabb& lhs, const Aabb& rhs) noexcept;
	Sphere Transform(const Sphere& sphere, const Matrix& matrix) noexcept;
	Sphere CreateSphereFromPoints(size_t count, const Vector3* points, size_t stride) noexcept;
	Sphere CreateSphere(const Aabb& aabb) noexcept;
}

namespace gglab
{
	using math::Aabb;
	using math::Sphere;
}
