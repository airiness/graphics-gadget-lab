#pragma once
#include "Core/Math/BoundingVolumes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gglab::math
{
	enum class FrustumPlane : uint8_t
	{
		Left,
		Right,
		Bottom,
		Top,
		Near,
		Far,
		Count,
	};

	struct Plane
	{
		constexpr Plane() noexcept = default;
		constexpr Plane(const Vector3& normal, float distance) noexcept :
			m_Normal(normal),
			m_Distance(distance)
		{
		}

		Vector3 m_Normal = Vector3::Zero;
		float m_Distance = 0.0f;
	};

	struct Frustum
	{
		static constexpr size_t PlaneCount = static_cast<size_t>(FrustumPlane::Count);

		Plane& operator[](FrustumPlane plane) noexcept
		{
			return m_Planes[static_cast<size_t>(plane)];
		}

		const Plane& operator[](FrustumPlane plane) const noexcept
		{
			return m_Planes[static_cast<size_t>(plane)];
		}

		std::array<Plane, PlaneCount> m_Planes{};
	};

	Plane Normalize(const Plane& plane, float tolerance = 1.0e-6f) noexcept;
	float SignedDistance(const Plane& plane, const Vector3& point) noexcept;
	Frustum CreateFrustumFromViewProjection(const Matrix& viewProjection) noexcept;
	[[nodiscard]] bool Intersects(const Plane& plane, const Sphere& sphere) noexcept;
	[[nodiscard]] bool Intersects(const Plane& plane, const Aabb& aabb) noexcept;
	[[nodiscard]] bool Intersects(const Frustum& frustum, const Sphere& sphere) noexcept;
	[[nodiscard]] bool Intersects(const Frustum& frustum, const Aabb& aabb) noexcept;
}

namespace gglab
{
	using math::Frustum;
	using math::FrustumPlane;
	using math::Plane;
}
