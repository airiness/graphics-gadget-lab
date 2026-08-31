#include "GGLabRuntime/Core/Math/Culling.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Math/MathFunctions.h"

namespace gglab::math
{
	namespace
	{
		[[nodiscard]] Plane MakePlane(float x, float y, float z, float distance) noexcept
		{
			return Normalize(Plane(Vector3(x, y, z), distance));
		}
	}

	Plane Normalize(const Plane& plane, float tolerance) noexcept
	{
		if (!IsFinite(plane.m_Normal) || !IsFinite(plane.m_Distance) ||
			!std::isfinite(tolerance) || tolerance < 0.0f)
		{
			return {};
		}

		const float length = plane.m_Normal.Length();
		if (!std::isfinite(length) || length <= tolerance)
		{
			return {};
		}

		const float invLength = 1.0f / length;
		return Plane(plane.m_Normal * invLength, plane.m_Distance * invLength);
	}

	float SignedDistance(const Plane& plane, const Vector3& point) noexcept
	{
		if (!IsFinite(plane.m_Normal) || !IsFinite(plane.m_Distance) || !IsFinite(point))
		{
			return 0.0f;
		}
		return plane.m_Normal.Dot(point) + plane.m_Distance;
	}

	bool TryHomogeneousDivide(const Vector4& value, Vector3& result, float tolerance) noexcept
	{
		result = Vector3::Zero;
		if (!IsFinite(value) ||
			!std::isfinite(tolerance) || tolerance < 0.0f ||
			std::abs(value.m_W) <= tolerance)
		{
			return false;
		}

		const Vector3 divided = Vector3(value.m_X, value.m_Y, value.m_Z) / value.m_W;
		if (!IsFinite(divided))
		{
			return false;
		}

		result = divided;
		return true;
	}

	std::array<Vector3, 8> BuildFrustumCornersFromInverseViewProjection(
		const Matrix& inverseViewProjection) noexcept
	{
		constexpr std::array<Vector4, 8> clipCorners = {
			Vector4(-1.0f, 1.0f, 0.0f, 1.0f),
			Vector4(1.0f, 1.0f, 0.0f, 1.0f),
			Vector4(1.0f, -1.0f, 0.0f, 1.0f),
			Vector4(-1.0f, -1.0f, 0.0f, 1.0f),
			Vector4(-1.0f, 1.0f, 1.0f, 1.0f),
			Vector4(1.0f, 1.0f, 1.0f, 1.0f),
			Vector4(1.0f, -1.0f, 1.0f, 1.0f),
			Vector4(-1.0f, -1.0f, 1.0f, 1.0f),
		};

		std::array<Vector3, 8> corners{};
		for (size_t index = 0; index < clipCorners.size(); ++index)
		{
			GGLAB_UNUSED(TryHomogeneousDivide(
				Transform(clipCorners[index], inverseViewProjection), corners[index]));
		}
		return corners;
	}

	Frustum CreateFrustumFromViewProjection(const Matrix& viewProjection) noexcept
	{
		Frustum frustum{};
		if (!IsFinite(viewProjection))
		{
			return frustum;
		}

		// Row-vector extraction for D3D clip space: -w <= x,y <= w and 0 <= z <= w.
		frustum[FrustumPlane::Left] = MakePlane(viewProjection.m_14 + viewProjection.m_11,
			viewProjection.m_24 + viewProjection.m_21, viewProjection.m_34 + viewProjection.m_31,
			viewProjection.m_44 + viewProjection.m_41);
		frustum[FrustumPlane::Right] = MakePlane(viewProjection.m_14 - viewProjection.m_11,
			viewProjection.m_24 - viewProjection.m_21, viewProjection.m_34 - viewProjection.m_31,
			viewProjection.m_44 - viewProjection.m_41);
		frustum[FrustumPlane::Bottom] = MakePlane(viewProjection.m_14 + viewProjection.m_12,
			viewProjection.m_24 + viewProjection.m_22, viewProjection.m_34 + viewProjection.m_32,
			viewProjection.m_44 + viewProjection.m_42);
		frustum[FrustumPlane::Top] = MakePlane(viewProjection.m_14 - viewProjection.m_12,
			viewProjection.m_24 - viewProjection.m_22, viewProjection.m_34 - viewProjection.m_32,
			viewProjection.m_44 - viewProjection.m_42);
		frustum[FrustumPlane::Near] = MakePlane(
			viewProjection.m_13, viewProjection.m_23, viewProjection.m_33, viewProjection.m_43);
		frustum[FrustumPlane::Far] = MakePlane(viewProjection.m_14 - viewProjection.m_13,
			viewProjection.m_24 - viewProjection.m_23, viewProjection.m_34 - viewProjection.m_33,
			viewProjection.m_44 - viewProjection.m_43);

		return frustum;
	}

	bool Intersects(const Plane& plane, const Sphere& sphere) noexcept
	{
		if (!IsFinite(plane.m_Normal) || !IsFinite(plane.m_Distance) ||
			!IsFinite(sphere.m_Center) ||
			!std::isfinite(sphere.m_Radius) || sphere.m_Radius < 0.0f)
		{
			return true;
		}
		return SignedDistance(plane, sphere.m_Center) >= -sphere.m_Radius;
	}

	bool Intersects(const Plane& plane, const Aabb& aabb) noexcept
	{
		if (!IsFinite(plane.m_Normal) || !IsFinite(plane.m_Distance) ||
			!IsFinite(aabb.m_Center) || !IsFinite(aabb.m_Extents))
		{
			return true;
		}

		const Vector3 extents(std::abs(aabb.m_Extents.m_X), std::abs(aabb.m_Extents.m_Y),
			std::abs(aabb.m_Extents.m_Z));
		const Vector3 absNormal(std::abs(plane.m_Normal.m_X), std::abs(plane.m_Normal.m_Y),
			std::abs(plane.m_Normal.m_Z));
		const float radius = extents.Dot(absNormal);
		return SignedDistance(plane, aabb.m_Center) >= -radius;
	}

	bool Intersects(const Frustum& frustum, const Sphere& sphere) noexcept
	{
		for (const Plane& plane : frustum.m_Planes)
		{
			if (!Intersects(plane, sphere))
			{
				return false;
			}
		}
		return true;
	}

	bool Intersects(const Frustum& frustum, const Aabb& aabb) noexcept
	{
		for (const Plane& plane : frustum.m_Planes)
		{
			if (!Intersects(plane, aabb))
			{
				return false;
			}
		}
		return true;
	}
}
