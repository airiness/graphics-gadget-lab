#pragma once
#include "Core/Math/BoundingVolumes.h"
#include "Core/Math/Color.h"
#include "Core/Math/Matrix.h"
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

	struct DebugDrawStyle
	{
		Color m_Color = Color::White;
		DebugDrawSpace m_Space = DebugDrawSpace::World;
		DebugDrawDepthMode m_DepthMode = DebugDrawDepthMode::Tested;
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

	struct DebugDrawFrameView
	{
		RHIBufferHandle m_VertexBuffer{};
		uint64_t m_VertexBufferOffset = 0;
		DebugDrawVertexRange m_Scene{};
		DebugDrawVertexRange m_OverlayWorld{};
		DebugDrawVertexRange m_OverlayScreen{};

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
			const DebugDrawStyle& style = {}) noexcept;
		void Axes(const Matrix& transform, float length = 1.0f,
			DebugDrawDepthMode depthMode = DebugDrawDepthMode::Tested) noexcept;
		void Aabb(const math::Aabb& bounds,
			const DebugDrawStyle& style = {}) noexcept;

	private:
		friend class DebugDrawSystem;
		explicit DebugDrawContext(DebugDrawSystem* system) noexcept : m_System(system) {}

		DebugDrawSystem* m_System = nullptr;
	};
}
