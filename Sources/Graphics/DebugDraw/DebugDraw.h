#pragma once
#include "Core/Math/BoundingVolumes.h"
#include "Core/Math/Color.h"
#include "Core/Math/Matrix.h"
#include "Core/StringId.h"
#include "Graphics/RHI/RHIHandles.h"

#include <cstdint>
#include <span>

namespace gglab
{
	class DebugDrawSystem;

	enum class DebugDrawSpace : uint8_t
	{
		World,
		Screen,
	};

	enum class DebugDrawDepthMode : uint8_t
	{
		Tested,
		Always,
	};

	enum class DebugDrawFillMode : uint8_t
	{
		Wireframe,
		Solid,
	};

	struct DebugDrawStyle
	{
		Color m_Color = Color::White;
		DebugDrawSpace m_Space = DebugDrawSpace::World;
		DebugDrawDepthMode m_DepthMode = DebugDrawDepthMode::Tested;
		DebugDrawFillMode m_FillMode = DebugDrawFillMode::Wireframe;
		StringID m_Channel{};
		float m_DurationSeconds = 0.0f;
	};

	struct DebugDrawVertex
	{
		Vector3 m_Position{};
		Color m_Color = Color::White;
	};
	static_assert(sizeof(DebugDrawVertex) == sizeof(float) * 7);

	struct DebugDrawVertexRange
	{
		uint32_t m_FirstVertex = 0;
		uint32_t m_VertexCount = 0;

		[[nodiscard]] bool IsEmpty() const noexcept { return m_VertexCount == 0; }
	};

	struct DebugDrawBatchRanges
	{
		DebugDrawVertexRange m_Lines{};
		DebugDrawVertexRange m_Triangles{};

		[[nodiscard]] bool IsEmpty() const noexcept
		{
			return m_Lines.IsEmpty() && m_Triangles.IsEmpty();
		}
	};

	struct DebugDrawStatistics
	{
		uint32_t m_SubmittedCommandCount = 0;
		uint32_t m_AcceptedCommandCount = 0;
		uint32_t m_DroppedCommandCount = 0;
		uint32_t m_InvalidCommandCount = 0;
		uint32_t m_LineVertexCount = 0;
		uint32_t m_TriangleVertexCount = 0;
		uint32_t m_PersistentCommandCount = 0;
	};

	struct DebugDrawFrameView
	{
		RHIBufferHandle m_VertexBuffer{};
		uint64_t m_VertexBufferOffset = 0;
		DebugDrawBatchRanges m_Scene{};
		DebugDrawBatchRanges m_OverlayWorld{};
		DebugDrawBatchRanges m_OverlayScreen{};
		DebugDrawStatistics m_Statistics{};

		[[nodiscard]] bool HasSceneDraws() const noexcept { return !m_Scene.IsEmpty(); }
		[[nodiscard]] bool HasOverlayDraws() const noexcept
		{
			return !m_OverlayWorld.IsEmpty() || !m_OverlayScreen.IsEmpty();
		}
	};

	class DebugDrawContext
	{
	public:
		void Line(const Vector3& start, const Vector3& end,
			const DebugDrawStyle& style = {}) noexcept;
		void Polyline(std::span<const Vector3> points, bool closed,
			const DebugDrawStyle& style = {}) noexcept;
		void Point(const Vector3& position, float size,
			const DebugDrawStyle& style = {}) noexcept;
		void Arrow(const Vector3& start, const Vector3& end, float headLength,
			const DebugDrawStyle& style = {}, uint32_t segments = 12) noexcept;
		void Axes(const Matrix& transform, float length = 1.0f,
			float headLength = 0.2f,
			const DebugDrawStyle& style = {
				.m_FillMode = DebugDrawFillMode::Solid,
			}) noexcept;
		void Aabb(const math::Aabb& bounds,
			const DebugDrawStyle& style = {}) noexcept;
		void Box(const Vector3& center, const Vector3& extents,
			const DebugDrawStyle& style = {}) noexcept;
		void Obb(const Matrix& transform, const Vector3& extents,
			const DebugDrawStyle& style = {}) noexcept;
		void Circle(const Vector3& center, const Vector3& normal, float radius,
			const DebugDrawStyle& style = {}, uint32_t segments = 32) noexcept;
		void Sphere(const Vector3& center, float radius,
			const DebugDrawStyle& style = {}, uint32_t segments = 24) noexcept;
		void Cone(const Vector3& apex, const Vector3& direction, float height, float radius,
			const DebugDrawStyle& style = {}, uint32_t segments = 24) noexcept;
		void Cylinder(const Vector3& center, const Vector3& axis, float height, float radius,
			const DebugDrawStyle& style = {}, uint32_t segments = 24) noexcept;
		void Capsule(const Vector3& center, const Vector3& axis, float halfHeight, float radius,
			const DebugDrawStyle& style = {}, uint32_t segments = 24) noexcept;
		void Frustum(std::span<const Vector3, 8> corners,
			const DebugDrawStyle& style = {}) noexcept;

		void SetChannelEnabled(StringID channel, bool enabled) noexcept;
		[[nodiscard]] bool IsChannelEnabled(StringID channel) const noexcept;
		void ClearChannel(StringID channel) noexcept;

	private:
		friend class DebugDrawSystem;
		explicit DebugDrawContext(DebugDrawSystem* system) noexcept : m_System(system) {}

		DebugDrawSystem* m_System = nullptr;
	};
}
