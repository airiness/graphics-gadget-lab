#include "Graphics/DebugDraw/DebugDrawSystem.h"
#include "Core/Math/MathConstants.h"
#include "Core/Math/MathFunctions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace gglab
{
	namespace
	{
		constexpr uint32_t MinSegments = 3;
		constexpr uint32_t MaxSegments = 128;

		struct Basis
		{
			Vector3 m_Axis{};
			Vector3 m_U{};
			Vector3 m_V{};
		};

		[[nodiscard]] bool BuildBasis(const Vector3& axis, Basis& basis) noexcept
		{
			if (!math::IsFinite(axis) || axis.LengthSquared() <= 1.0e-12f)
			{
				return false;
			}
			basis.m_Axis = axis.Normalized();
			const Vector3 reference = std::abs(basis.m_Axis.Dot(Vector3::UnitY)) < 0.95f
				? Vector3::UnitY
				: Vector3::UnitX;
			basis.m_U = reference.Cross(basis.m_Axis).Normalized();
			basis.m_V = basis.m_Axis.Cross(basis.m_U).Normalized();
			return true;
		}

		[[nodiscard]] uint32_t ClampSegments(uint32_t segments) noexcept
		{
			return std::clamp(segments, MinSegments, MaxSegments);
		}

		[[nodiscard]] Vector3 CirclePoint(
			const Vector3& center, const Basis& basis, float radius, float angle) noexcept
		{
			return center + (basis.m_U * std::cos(angle) + basis.m_V * std::sin(angle)) * radius;
		}

		void AppendLine(std::vector<Vector3>& positions, const Vector3& a, const Vector3& b)
		{
			positions.push_back(a);
			positions.push_back(b);
		}

		void AppendTriangle(
			std::vector<Vector3>& positions, const Vector3& a, const Vector3& b, const Vector3& c)
		{
			positions.push_back(a);
			positions.push_back(b);
			positions.push_back(c);
		}

		[[nodiscard]] std::array<Vector3, 8> BuildBoxCorners(
			const Vector3& center, const Vector3& extents) noexcept
		{
			const Vector3 min = center - extents;
			const Vector3 max = center + extents;
			return {
				Vector3(min.m_X, min.m_Y, min.m_Z),
				Vector3(max.m_X, min.m_Y, min.m_Z),
				Vector3(max.m_X, max.m_Y, min.m_Z),
				Vector3(min.m_X, max.m_Y, min.m_Z),
				Vector3(min.m_X, min.m_Y, max.m_Z),
				Vector3(max.m_X, min.m_Y, max.m_Z),
				Vector3(max.m_X, max.m_Y, max.m_Z),
				Vector3(min.m_X, max.m_Y, max.m_Z),
			};
		}

		void BuildBoxGeometry(std::span<const Vector3, 8> corners, DebugDrawFillMode fillMode,
			std::vector<Vector3>& positions)
		{
			if (fillMode == DebugDrawFillMode::Wireframe)
			{
				constexpr std::array<std::array<uint8_t, 2>, 12> edges = { {
					{0, 1},
					{1, 2},
					{2, 3},
					{3, 0},
					{4, 5},
					{5, 6},
					{6, 7},
					{7, 4},
					{0, 4},
					{1, 5},
					{2, 6},
					{3, 7},
				} };
				positions.reserve(edges.size() * 2);
				for (const auto& edge : edges)
				{
					AppendLine(positions, corners[edge[0]], corners[edge[1]]);
				}
				return;
			}

			constexpr std::array<std::array<uint8_t, 4>, 6> faces = { {
				{0, 3, 2, 1},
				{4, 5, 6, 7},
				{0, 1, 5, 4},
				{3, 7, 6, 2},
				{0, 4, 7, 3},
				{1, 2, 6, 5},
			} };
			positions.reserve(faces.size() * 6);
			for (const auto& face : faces)
			{
				AppendTriangle(positions, corners[face[0]], corners[face[1]], corners[face[2]]);
				AppendTriangle(positions, corners[face[0]], corners[face[2]], corners[face[3]]);
			}
		}
	}

	void DebugDrawContext::Box(
		const Vector3& center, const Vector3& extents, const DebugDrawStyle& style) noexcept
	{
		if (!m_System || !math::IsFinite(extents) ||
			extents.m_X < 0.0f || extents.m_Y < 0.0f || extents.m_Z < 0.0f)
		{
			if (m_System)
				m_System->RejectInvalid();
			return;
		}
		const auto corners = BuildBoxCorners(center, extents);
		std::vector<Vector3> positions;
		BuildBoxGeometry(corners, style.m_FillMode, positions);
		m_System->Submit(style.m_FillMode == DebugDrawFillMode::Wireframe
			? DebugDrawSystem::PrimitiveTopology::Lines
			: DebugDrawSystem::PrimitiveTopology::Triangles,
			positions, style);
	}

	void DebugDrawContext::Obb(
		const Matrix& transform, const Vector3& extents, const DebugDrawStyle& style) noexcept
	{
		if (!m_System || !math::IsFinite(extents) ||
			extents.m_X < 0.0f || extents.m_Y < 0.0f || extents.m_Z < 0.0f)
		{
			if (m_System)
				m_System->RejectInvalid();
			return;
		}
		auto corners = BuildBoxCorners(Vector3::Zero, extents);
		for (Vector3& corner : corners)
		{
			corner = math::TransformPoint(corner, transform);
		}
		std::vector<Vector3> positions;
		BuildBoxGeometry(corners, style.m_FillMode, positions);
		m_System->Submit(style.m_FillMode == DebugDrawFillMode::Wireframe
			? DebugDrawSystem::PrimitiveTopology::Lines
			: DebugDrawSystem::PrimitiveTopology::Triangles,
			positions, style);
	}

	void DebugDrawContext::Circle(const Vector3& center, const Vector3& normal, float radius,
		const DebugDrawStyle& style, uint32_t segments) noexcept
	{
		Basis basis{};
		if (!m_System || !BuildBasis(normal, basis) || !std::isfinite(radius) || radius <= 0.0f)
		{
			if (m_System)
				m_System->RejectInvalid();
			return;
		}
		segments = ClampSegments(segments);
		std::vector<Vector3> positions;
		if (style.m_FillMode == DebugDrawFillMode::Wireframe)
		{
			positions.reserve(segments * 2);
			for (uint32_t segment = 0; segment < segments; ++segment)
			{
				const float a0 = math::TwoPi * segment / segments;
				const float a1 = math::TwoPi * (segment + 1) / segments;
				AppendLine(positions, CirclePoint(center, basis, radius, a0),
					CirclePoint(center, basis, radius, a1));
			}
			m_System->Submit(DebugDrawSystem::PrimitiveTopology::Lines, positions, style);
			return;
		}

		positions.reserve(segments * 3);
		for (uint32_t segment = 0; segment < segments; ++segment)
		{
			const float a0 = math::TwoPi * segment / segments;
			const float a1 = math::TwoPi * (segment + 1) / segments;
			AppendTriangle(positions, center, CirclePoint(center, basis, radius, a0),
				CirclePoint(center, basis, radius, a1));
		}
		m_System->Submit(DebugDrawSystem::PrimitiveTopology::Triangles, positions, style);
	}

	void DebugDrawContext::Sphere(const Vector3& center, float radius, const DebugDrawStyle& style,
		uint32_t segments) noexcept
	{
		if (!m_System || !math::IsFinite(center) || !std::isfinite(radius) || radius <= 0.0f)
		{
			if (m_System)
				m_System->RejectInvalid();
			return;
		}
		segments = ClampSegments(segments);
		const uint32_t latitudeSegments = std::max(3u, segments / 2u);
		auto point = [=](uint32_t latitude, uint32_t longitude) noexcept
			{
				const float phi = -math::Pi * 0.5f + math::Pi * latitude / latitudeSegments;
				const float theta = math::TwoPi * longitude / segments;
				const float ringRadius = std::cos(phi) * radius;
				return center + Vector3(std::cos(theta) * ringRadius, std::sin(phi) * radius,
					std::sin(theta) * ringRadius);
			};

		std::vector<Vector3> positions;
		if (style.m_FillMode == DebugDrawFillMode::Wireframe)
		{
			for (uint32_t latitude = 1; latitude < latitudeSegments; ++latitude)
			{
				for (uint32_t longitude = 0; longitude < segments; ++longitude)
				{
					AppendLine(
						positions, point(latitude, longitude), point(latitude, longitude + 1));
				}
			}
			for (uint32_t longitude = 0; longitude < segments; ++longitude)
			{
				for (uint32_t latitude = 0; latitude < latitudeSegments; ++latitude)
				{
					AppendLine(
						positions, point(latitude, longitude), point(latitude + 1, longitude));
				}
			}
			m_System->Submit(DebugDrawSystem::PrimitiveTopology::Lines, positions, style);
			return;
		}

		for (uint32_t latitude = 0; latitude < latitudeSegments; ++latitude)
		{
			for (uint32_t longitude = 0; longitude < segments; ++longitude)
			{
				const Vector3 a = point(latitude, longitude);
				const Vector3 b = point(latitude + 1, longitude);
				const Vector3 c = point(latitude + 1, longitude + 1);
				const Vector3 d = point(latitude, longitude + 1);
				if (latitude == 0)
				{
					AppendTriangle(positions, a, b, c);
				}
				else if (latitude + 1 == latitudeSegments)
				{
					AppendTriangle(positions, a, b, d);
				}
				else
				{
					AppendTriangle(positions, a, b, c);
					AppendTriangle(positions, a, c, d);
				}
			}
		}
		m_System->Submit(DebugDrawSystem::PrimitiveTopology::Triangles, positions, style);
	}

	void DebugDrawContext::Cone(const Vector3& apex, const Vector3& direction, float height,
		float radius, const DebugDrawStyle& style, uint32_t segments) noexcept
	{
		Basis basis{};
		if (!m_System || !BuildBasis(direction, basis) ||
			!std::isfinite(height) || !std::isfinite(radius) ||
			height <= 0.0f || radius <= 0.0f)
		{
			if (m_System)
				m_System->RejectInvalid();
			return;
		}
		segments = ClampSegments(segments);
		const Vector3 baseCenter = apex + basis.m_Axis * height;
		std::vector<Vector3> positions;
		for (uint32_t segment = 0; segment < segments; ++segment)
		{
			const Vector3 p0 =
				CirclePoint(baseCenter, basis, radius, math::TwoPi * segment / segments);
			const Vector3 p1 =
				CirclePoint(baseCenter, basis, radius, math::TwoPi * (segment + 1) / segments);
			if (style.m_FillMode == DebugDrawFillMode::Wireframe)
			{
				AppendLine(positions, p0, p1);
				AppendLine(positions, apex, p0);
			}
			else
			{
				AppendTriangle(positions, apex, p0, p1);
				AppendTriangle(positions, baseCenter, p1, p0);
			}
		}
		m_System->Submit(style.m_FillMode == DebugDrawFillMode::Wireframe
			? DebugDrawSystem::PrimitiveTopology::Lines
			: DebugDrawSystem::PrimitiveTopology::Triangles,
			positions, style);
	}

	void DebugDrawContext::Cylinder(const Vector3& center, const Vector3& axis, float height,
		float radius, const DebugDrawStyle& style, uint32_t segments) noexcept
	{
		Basis basis{};
		if (!m_System || !BuildBasis(axis, basis) ||
			!std::isfinite(height) || !std::isfinite(radius) ||
			height <= 0.0f || radius <= 0.0f)
		{
			if (m_System)
				m_System->RejectInvalid();
			return;
		}
		segments = ClampSegments(segments);
		const Vector3 bottomCenter = center - basis.m_Axis * (height * 0.5f);
		const Vector3 topCenter = center + basis.m_Axis * (height * 0.5f);
		std::vector<Vector3> positions;
		for (uint32_t segment = 0; segment < segments; ++segment)
		{
			const float a0 = math::TwoPi * segment / segments;
			const float a1 = math::TwoPi * (segment + 1) / segments;
			const Vector3 b0 = CirclePoint(bottomCenter, basis, radius, a0);
			const Vector3 b1 = CirclePoint(bottomCenter, basis, radius, a1);
			const Vector3 t0 = CirclePoint(topCenter, basis, radius, a0);
			const Vector3 t1 = CirclePoint(topCenter, basis, radius, a1);
			if (style.m_FillMode == DebugDrawFillMode::Wireframe)
			{
				AppendLine(positions, b0, b1);
				AppendLine(positions, t0, t1);
				AppendLine(positions, b0, t0);
			}
			else
			{
				AppendTriangle(positions, b0, t0, t1);
				AppendTriangle(positions, b0, t1, b1);
				AppendTriangle(positions, bottomCenter, b1, b0);
				AppendTriangle(positions, topCenter, t0, t1);
			}
		}
		m_System->Submit(style.m_FillMode == DebugDrawFillMode::Wireframe
			? DebugDrawSystem::PrimitiveTopology::Lines
			: DebugDrawSystem::PrimitiveTopology::Triangles,
			positions, style);
	}

	void DebugDrawContext::Capsule(const Vector3& center, const Vector3& axis, float halfHeight,
		float radius, const DebugDrawStyle& style, uint32_t segments) noexcept
	{
		Basis basis{};
		if (!m_System || !BuildBasis(axis, basis) ||
			!std::isfinite(halfHeight) || !std::isfinite(radius) ||
			halfHeight < 0.0f || radius <= 0.0f)
		{
			if (m_System)
				m_System->RejectInvalid();
			return;
		}
		if (halfHeight <= 1.0e-6f)
		{
			Sphere(center, radius, style, segments);
			return;
		}
		segments = ClampSegments(segments);
		const uint32_t hemisphereSegments = std::max(2u, segments / 4u);
		struct Ring
		{
			float m_Axial;
			float m_Radius;
		};
		std::vector<Ring> rings;
		for (uint32_t index = 0; index <= hemisphereSegments; ++index)
		{
			const float angle = -math::Pi * 0.5f + math::Pi * 0.5f * index / hemisphereSegments;
			rings.push_back({ -halfHeight + std::sin(angle) * radius, std::cos(angle) * radius });
		}
		for (uint32_t index = 0; index <= hemisphereSegments; ++index)
		{
			const float angle = math::Pi * 0.5f * index / hemisphereSegments;
			rings.push_back({ halfHeight + std::sin(angle) * radius, std::cos(angle) * radius });
		}
		auto point = [&](size_t ringIndex, uint32_t segment) noexcept
			{
				const Ring& ring = rings[ringIndex];
				const float angle = math::TwoPi * segment / segments;
				return center + basis.m_Axis * ring.m_Axial +
					(basis.m_U * std::cos(angle) + basis.m_V * std::sin(angle)) * ring.m_Radius;
			};

		std::vector<Vector3> positions;
		if (style.m_FillMode == DebugDrawFillMode::Wireframe)
		{
			for (size_t ring = 0; ring < rings.size(); ++ring)
			{
				if (rings[ring].m_Radius > 1.0e-6f)
				{
					for (uint32_t segment = 0; segment < segments; ++segment)
					{
						AppendLine(positions, point(ring, segment), point(ring, segment + 1));
					}
				}
			}
			for (size_t ring = 0; ring + 1 < rings.size(); ++ring)
			{
				for (uint32_t segment = 0; segment < segments; ++segment)
				{
					AppendLine(positions, point(ring, segment), point(ring + 1, segment));
				}
			}
			m_System->Submit(DebugDrawSystem::PrimitiveTopology::Lines, positions, style);
			return;
		}

		for (size_t ring = 0; ring + 1 < rings.size(); ++ring)
		{
			for (uint32_t segment = 0; segment < segments; ++segment)
			{
				const Vector3 a = point(ring, segment);
				const Vector3 b = point(ring + 1, segment);
				const Vector3 c = point(ring + 1, segment + 1);
				const Vector3 d = point(ring, segment + 1);
				if (rings[ring].m_Radius <= 1.0e-6f)
				{
					AppendTriangle(positions, a, b, c);
				}
				else if (rings[ring + 1].m_Radius <= 1.0e-6f)
				{
					AppendTriangle(positions, a, b, d);
				}
				else
				{
					AppendTriangle(positions, a, b, c);
					AppendTriangle(positions, a, c, d);
				}
			}
		}
		m_System->Submit(DebugDrawSystem::PrimitiveTopology::Triangles, positions, style);
	}

	void DebugDrawContext::Frustum(
		std::span<const Vector3, 8> corners, const DebugDrawStyle& style) noexcept
	{
		if (!m_System)
		{
			return;
		}
		std::vector<Vector3> positions;
		BuildBoxGeometry(corners, style.m_FillMode, positions);
		m_System->Submit(style.m_FillMode == DebugDrawFillMode::Wireframe
			? DebugDrawSystem::PrimitiveTopology::Lines
			: DebugDrawSystem::PrimitiveTopology::Triangles,
			positions, style);
	}

	void DebugDrawContext::Grid(const Vector3& center, const Vector3& normal,
		const Vector3& tangent, float halfExtent, uint32_t divisions,
		const DebugDrawStyle& style) noexcept
	{
		Basis basis{};
		if (!m_System || !BuildBasis(normal, basis) || !math::IsFinite(tangent) ||
			!std::isfinite(halfExtent) || halfExtent <= 0.0f ||
			divisions == 0)
		{
			if (m_System)
				m_System->RejectInvalid();
			return;
		}

		Vector3 axisX = tangent - basis.m_Axis * tangent.Dot(basis.m_Axis);
		axisX = math::SafeNormalize(axisX, basis.m_U);
		const Vector3 axisY = basis.m_Axis.Cross(axisX).Normalized();
		divisions = std::min(divisions, 256u);
		const float step = halfExtent * 2.0f / static_cast<float>(divisions);
		std::vector<Vector3> positions;
		positions.reserve(static_cast<size_t>(divisions + 1) * 4u);
		for (uint32_t index = 0; index <= divisions; ++index)
		{
			const float offset = -halfExtent + step * index;
			AppendLine(positions, center + axisX * offset - axisY * halfExtent,
				center + axisX * offset + axisY * halfExtent);
			AppendLine(positions, center + axisY * offset - axisX * halfExtent,
				center + axisY * offset + axisX * halfExtent);
		}
		m_System->Submit(DebugDrawSystem::PrimitiveTopology::Lines, positions, style);
	}

	void DebugDrawContext::SetChannelEnabled(StringID channel, bool enabled) noexcept
	{
		if (m_System)
			m_System->SetChannelEnabled(channel, enabled);
	}

	bool DebugDrawContext::IsChannelEnabled(StringID channel) const noexcept
	{
		return m_System && m_System->IsChannelEnabled(channel);
	}

	std::vector<DebugDrawChannelState> DebugDrawContext::GetChannelStates() const noexcept
	{
		return m_System ? m_System->GetChannelStates() : std::vector<DebugDrawChannelState>{};
	}

	void DebugDrawContext::ClearChannel(StringID channel) noexcept
	{
		if (m_System)
			m_System->ClearChannel(channel);
	}
}
