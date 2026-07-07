#include "Core/Precompiled.h"
#include "Core/Math/MathFunctions.h"
#include "Graphics/DebugDraw/DebugDrawSystem.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIDevice.h"

namespace gglab
{
	DebugDrawSystem::DebugDrawSystem(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_Context(this),
		m_FrameSlotCount(createInfo.m_FrameSlotCount),
		m_MaxLineCountPerFrame(createInfo.m_MaxLineCountPerFrame)
	{
		GGLAB_ASSERT_NOT_NULL(m_Device);
		GGLAB_ASSERT(m_FrameSlotCount > 0);
		GGLAB_ASSERT(m_MaxLineCountPerFrame > 0);

		m_FrameSlotSizeInBytes = static_cast<uint64_t>(sizeof(DebugDrawVertex)) *
			m_MaxLineCountPerFrame * 2u;
		m_TotalBufferSizeInBytes = m_FrameSlotSizeInBytes * m_FrameSlotCount;

		RHIBufferDesc desc{};
		desc.m_SizeInBytes = m_TotalBufferSizeInBytes;
		desc.m_StrideInBytes = sizeof(DebugDrawVertex);
		desc.m_Usage = RHIBufferUsage::Vertex;
		desc.m_MemoryUsage = RHIMemoryUsage::CpuToGpu;
		desc.m_DebugName = "DebugDraw.VertexBuffer";
		m_VertexBuffer = RHIBufferOwner(m_Device, m_Device->CreateBuffer(desc));
		GGLAB_ASSERT_MSG(m_VertexBuffer, "DebugDraw failed to create its vertex buffer.");

		if (m_VertexBuffer)
		{
			m_MappedVertices = static_cast<std::byte*>(
				m_Device->MapBuffer(m_VertexBuffer.Get(), {}));
		}
		GGLAB_ASSERT_MSG(m_MappedVertices, "DebugDraw failed to map its vertex buffer.");

		m_PendingLines.reserve(m_MaxLineCountPerFrame);
		m_SealedLines.reserve(m_MaxLineCountPerFrame);
		m_StagingVertices.reserve(static_cast<size_t>(m_MaxLineCountPerFrame) * 2u);
	}

	DebugDrawSystem::~DebugDrawSystem() noexcept
	{
		if (m_MappedVertices && m_VertexBuffer)
		{
			m_Device->UnmapBuffer(m_VertexBuffer.Get(), { 0, m_TotalBufferSizeInBytes });
			m_MappedVertices = nullptr;
		}
	}

	const DebugDrawFrameView& DebugDrawSystem::SealFrame(uint32_t frameSlot) noexcept
	{
		GGLAB_ASSERT(frameSlot < m_FrameSlotCount);
		m_FrameView = {};
		if (frameSlot >= m_FrameSlotCount || !m_MappedVertices || !m_VertexBuffer)
		{
			return m_FrameView;
		}

		{
			std::scoped_lock lock(m_Mutex);
			m_SealedLines.clear();
			m_SealedLines.swap(m_PendingLines);
			m_BudgetWarningEmitted = false;
		}

		m_StagingVertices.clear();
		auto appendRange = [this](auto predicate) noexcept
			{
				DebugDrawVertexRange range{};
				range.m_FirstVertex = static_cast<uint32_t>(m_StagingVertices.size());
				for (const LineCommand& line : m_SealedLines)
				{
					if (!predicate(line.m_Style))
					{
						continue;
					}
					m_StagingVertices.push_back({ line.m_Start, line.m_Style.m_Color });
					m_StagingVertices.push_back({ line.m_End, line.m_Style.m_Color });
				}
				range.m_VertexCount =
					static_cast<uint32_t>(m_StagingVertices.size()) - range.m_FirstVertex;
				return range;
			};

		m_FrameView.m_Scene = appendRange([](const DebugDrawStyle& style) noexcept
			{
				return style.m_Space == DebugDrawSpace::World &&
					style.m_DepthMode == DebugDrawDepthMode::Tested;
			});
		m_FrameView.m_OverlayWorld = appendRange([](const DebugDrawStyle& style) noexcept
			{
				return style.m_Space == DebugDrawSpace::World &&
					style.m_DepthMode == DebugDrawDepthMode::Always;
			});
		m_FrameView.m_OverlayScreen = appendRange([](const DebugDrawStyle& style) noexcept
			{
				return style.m_Space == DebugDrawSpace::Screen;
			});

		m_FrameView.m_VertexBuffer = m_VertexBuffer.Get();
		m_FrameView.m_VertexBufferOffset = m_FrameSlotSizeInBytes * frameSlot;
		if (!m_StagingVertices.empty())
		{
			const size_t sizeInBytes = m_StagingVertices.size() * sizeof(DebugDrawVertex);
			GGLAB_ASSERT(sizeInBytes <= m_FrameSlotSizeInBytes);
			std::memcpy(
				m_MappedVertices + m_FrameView.m_VertexBufferOffset,
				m_StagingVertices.data(),
				sizeInBytes);
		}

		return m_FrameView;
	}

	void DebugDrawSystem::Clear() noexcept
	{
		std::scoped_lock lock(m_Mutex);
		m_PendingLines.clear();
	}

	void DebugDrawSystem::Submit(std::span<const LineCommand> lines) noexcept
	{
		if (lines.empty())
		{
			return;
		}

		std::scoped_lock lock(m_Mutex);
		size_t droppedCount = 0;
		for (const LineCommand& line : lines)
		{
			if (!IsValid(line))
			{
				continue;
			}
			if (m_PendingLines.size() >= m_MaxLineCountPerFrame)
			{
				++droppedCount;
				continue;
			}
			m_PendingLines.push_back(line);
		}
		if (droppedCount > 0 && !m_BudgetWarningEmitted)
		{
			m_BudgetWarningEmitted = true;
			GGLAB_LOG_GRAPHICS_WARN(
				"DebugDraw line budget exceeded; dropped {} lines.",
				droppedCount);
		}
	}

	bool DebugDrawSystem::IsValid(const LineCommand& line) const noexcept
	{
		return math::IsFinite(line.m_Start) &&
			math::IsFinite(line.m_End) &&
			math::IsFinite(line.m_Style.m_Color);
	}

	void DebugDrawContext::Line(const Vector3& start, const Vector3& end,
		const DebugDrawStyle& style) noexcept
	{
		if (m_System)
		{
			const DebugDrawSystem::LineCommand line{ start, end, style };
			m_System->Submit(std::span<const DebugDrawSystem::LineCommand>(&line, 1));
		}
	}

	void DebugDrawContext::Polyline(std::span<const Vector3> points, bool closed,
		const DebugDrawStyle& style) noexcept
	{
		if (!m_System || points.size() < 2)
		{
			return;
		}
		std::vector<DebugDrawSystem::LineCommand> lines;
		lines.reserve(points.size() - 1 + (closed ? 1 : 0));
		for (size_t index = 1; index < points.size(); ++index)
		{
			lines.push_back({ points[index - 1], points[index], style });
		}
		if (closed)
		{
			lines.push_back({ points.back(), points.front(), style });
		}
		m_System->Submit(lines);
	}

	void DebugDrawContext::Point(const Vector3& position, float size,
		const DebugDrawStyle& style) noexcept
	{
		if (!m_System || !std::isfinite(size) || size <= 0.0f)
		{
			return;
		}
		const Vector3 x(size, 0.0f, 0.0f);
		const Vector3 y(0.0f, size, 0.0f);
		const Vector3 z(0.0f, 0.0f, size);
		const std::array<DebugDrawSystem::LineCommand, 3> lines = {
			DebugDrawSystem::LineCommand{ position - x, position + x, style },
			DebugDrawSystem::LineCommand{ position - y, position + y, style },
			DebugDrawSystem::LineCommand{ position - z, position + z, style },
		};
		m_System->Submit(lines);
	}

	void DebugDrawContext::Arrow(const Vector3& start, const Vector3& end,
		float headLength, const DebugDrawStyle& style) noexcept
	{
		if (!m_System || !std::isfinite(headLength) || headLength <= 0.0f)
		{
			return;
		}
		Vector3 direction = end - start;
		const float length = direction.Length();
		if (!std::isfinite(length) || length <= 1.0e-6f)
		{
			return;
		}
		direction /= length;
		headLength = std::min(headLength, length);
		const Vector3 reference = std::abs(direction.Dot(Vector3::UnitY)) < 0.95f ?
			Vector3::UnitY : Vector3::UnitX;
		Vector3 side = direction.Cross(reference).Normalized();
		Vector3 up = side.Cross(direction).Normalized();
		const Vector3 headCenter = end - direction * headLength;
		const float radius = headLength * 0.45f;
		const std::array<DebugDrawSystem::LineCommand, 5> lines = {
			DebugDrawSystem::LineCommand{ start, end, style },
			DebugDrawSystem::LineCommand{ end, headCenter + side * radius, style },
			DebugDrawSystem::LineCommand{ end, headCenter - side * radius, style },
			DebugDrawSystem::LineCommand{ end, headCenter + up * radius, style },
			DebugDrawSystem::LineCommand{ end, headCenter - up * radius, style },
		};
		m_System->Submit(lines);
	}

	void DebugDrawContext::Axes(const Matrix& transform, float length,
		DebugDrawDepthMode depthMode) noexcept
	{
		if (!m_System || !std::isfinite(length) || length <= 0.0f)
		{
			return;
		}
		const Vector3 origin = math::TransformPoint(Vector3::Zero, transform);
		const std::array<DebugDrawSystem::LineCommand, 3> lines = {
			DebugDrawSystem::LineCommand{ origin, math::TransformPoint(Vector3::UnitX * length, transform),
				{.m_Color = Color::Red, .m_DepthMode = depthMode } },
			DebugDrawSystem::LineCommand{ origin, math::TransformPoint(Vector3::UnitY * length, transform),
				{.m_Color = Color::Green, .m_DepthMode = depthMode } },
			DebugDrawSystem::LineCommand{ origin, math::TransformPoint(Vector3::UnitZ * length, transform),
				{.m_Color = Color::Blue, .m_DepthMode = depthMode } },
		};
		m_System->Submit(lines);
	}

	void DebugDrawContext::Aabb(const math::Aabb& bounds,
		const DebugDrawStyle& style) noexcept
	{
		if (!m_System)
		{
			return;
		}
		const Vector3 min = bounds.m_Center - bounds.m_Extents;
		const Vector3 max = bounds.m_Center + bounds.m_Extents;
		const std::array<Vector3, 8> corners = {
			Vector3(min.m_X, min.m_Y, min.m_Z), Vector3(max.m_X, min.m_Y, min.m_Z),
			Vector3(max.m_X, max.m_Y, min.m_Z), Vector3(min.m_X, max.m_Y, min.m_Z),
			Vector3(min.m_X, min.m_Y, max.m_Z), Vector3(max.m_X, min.m_Y, max.m_Z),
			Vector3(max.m_X, max.m_Y, max.m_Z), Vector3(min.m_X, max.m_Y, max.m_Z),
		};
		constexpr std::array<std::array<uint8_t, 2>, 12> edges = { {
			{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
			{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
		} };
		std::array<DebugDrawSystem::LineCommand, edges.size()> lines{};
		for (size_t index = 0; index < edges.size(); ++index)
		{
			lines[index] = { corners[edges[index][0]], corners[edges[index][1]], style };
		}
		m_System->Submit(lines);
	}
}
