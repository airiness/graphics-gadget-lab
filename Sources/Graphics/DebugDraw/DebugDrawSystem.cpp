#include "Graphics/DebugDraw/DebugDrawSystem.h"
#include "Core/CoreMacros.h"
#include "Core/Log/LogMacros.h"
#include "Core/Math/MathFunctions.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIDevice.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gglab
{
	DebugDrawSystem::DebugDrawSystem(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device), m_Context(this),
		m_FrameSlotCount(createInfo.m_FrameSlotCount),
		m_MaxVertexCountPerFrame(createInfo.m_MaxVertexCountPerFrame)
	{
		GGLAB_ASSERT_NOT_NULL(m_Device);
		GGLAB_ASSERT(m_FrameSlotCount > 0);
		GGLAB_ASSERT(m_MaxVertexCountPerFrame > 0);

		m_FrameSlotSizeInBytes =
			static_cast<uint64_t>(sizeof(DebugDrawVertex)) * m_MaxVertexCountPerFrame;
		m_TotalBufferSizeInBytes = m_FrameSlotSizeInBytes * m_FrameSlotCount;

		RHIBufferDesc desc{};
		desc.m_SizeInBytes = m_TotalBufferSizeInBytes;
		desc.m_StrideInBytes = sizeof(DebugDrawVertex);
		desc.m_Usage = RHIBufferUsage::Vertex;
		desc.m_MemoryUsage = RHIMemoryUsage::CpuToGpu;
		const RHIResourceDebugIdentityDesc debugIdentity{
			.m_Domain = RHIResourceDebugDomain::Renderer,
			.m_Category = "DebugDraw.VertexBuffer",
			.m_Label = "FrameVertices",
		};
		m_VertexBuffer = RHIBufferOwner(m_Device, m_Device->CreateBuffer(desc, debugIdentity));
		GGLAB_ASSERT_MSG(m_VertexBuffer, "DebugDraw failed to create its vertex buffer.");

		if (m_VertexBuffer)
		{
			m_MappedVertices =
				static_cast<std::byte*>(m_Device->MapBuffer(m_VertexBuffer.Get(), {}));
		}
		GGLAB_ASSERT_MSG(m_MappedVertices, "DebugDraw failed to map its vertex buffer.");

		m_PendingCommands.reserve(1024);
		m_SealedCommands.reserve(1024);
		m_PersistentCommands.reserve(256);
		m_StagingVertices.reserve(m_MaxVertexCountPerFrame);
	}

	DebugDrawSystem::~DebugDrawSystem() noexcept
	{
		if (m_MappedVertices && m_VertexBuffer)
		{
			m_Device->UnmapBuffer(m_VertexBuffer.Get(), { 0, m_TotalBufferSizeInBytes });
			m_MappedVertices = nullptr;
		}
	}

	const DebugDrawFrameView& DebugDrawSystem::SealFrame(
		uint32_t frameSlot, float deltaTime, const DebugDrawCullContext& cullContext) noexcept
	{
		GGLAB_ASSERT(frameSlot < m_FrameSlotCount);
		m_FrameView = {};
		if (frameSlot >= m_FrameSlotCount || !m_MappedVertices || !m_VertexBuffer)
		{
			return m_FrameView;
		}

		std::unordered_set<StringID> disabledChannels;
		{
			std::scoped_lock lock(m_Mutex);
			disabledChannels = m_DisabledChannels;
			m_SealedCommands.clear();
			for (Command& command : m_PersistentCommands)
			{
				m_SealedCommands.push_back(command);
				command.m_RemainingSeconds -= std::max(deltaTime, 0.0f);
			}
			std::erase_if(m_PersistentCommands,
				[](const Command& command) noexcept { return command.m_RemainingSeconds <= 0.0f; });
			m_PersistentVertexCount = 0;
			for (const Command& command : m_PersistentCommands)
			{
				m_PersistentVertexCount += static_cast<uint32_t>(command.m_Vertices->size());
			}

			for (Command& command : m_PendingCommands)
			{
				m_SealedCommands.push_back(command);
				if (command.m_Style.m_DurationSeconds > 0.0f)
				{
					const uint32_t vertexCount = static_cast<uint32_t>(command.m_Vertices->size());
					if (m_PersistentVertexCount + vertexCount <= m_MaxVertexCountPerFrame)
					{
						command.m_RemainingSeconds = command.m_Style.m_DurationSeconds;
						m_PersistentCommands.push_back(command);
						m_PersistentVertexCount += vertexCount;
					}
					else
					{
						++m_PendingStatistics.m_DroppedCommandCount;
					}
				}
			}
			m_PendingCommands.clear();
			m_PendingVertexCount = 0;
			m_FrameView.m_Statistics = m_PendingStatistics;
			m_FrameView.m_Statistics.m_PersistentCommandCount =
				static_cast<uint32_t>(m_PersistentCommands.size());
			m_PendingStatistics = {};
			m_BudgetWarningEmitted = false;
		}

		m_StagingVertices.clear();
		auto appendRange = [this, &disabledChannels, &cullContext](
			auto predicate, PrimitiveTopology topology) noexcept
			{
				DebugDrawVertexRange range{};
				range.m_FirstVertex = static_cast<uint32_t>(m_StagingVertices.size());
				for (const Command& command : m_SealedCommands)
				{
					if (command.m_Topology != topology || !command.m_Vertices ||
						!predicate(command.m_Style))
					{
						continue;
					}
					if (disabledChannels.contains(command.m_Style.m_Channel))
					{
						++m_FrameView.m_Statistics.m_ChannelFilteredCommandCount;
						continue;
					}
					if (ShouldCull(command, cullContext))
					{
						++m_FrameView.m_Statistics.m_CulledCommandCount;
						continue;
					}
					if (m_StagingVertices.size() + command.m_Vertices->size() >
						m_MaxVertexCountPerFrame)
					{
						++m_FrameView.m_Statistics.m_DroppedCommandCount;
						continue;
					}
					m_StagingVertices.insert(m_StagingVertices.end(), command.m_Vertices->begin(),
						command.m_Vertices->end());
				}
				range.m_VertexCount =
					static_cast<uint32_t>(m_StagingVertices.size()) - range.m_FirstVertex;
				return range;
			};

		auto fillBatch = [&appendRange](DebugDrawBatchRanges& batch, auto predicate) noexcept
			{
				batch.m_Lines = appendRange(predicate, PrimitiveTopology::Lines);
				batch.m_Triangles = appendRange(predicate, PrimitiveTopology::Triangles);
			};
		fillBatch(m_FrameView.m_Scene,
			[](const DebugDrawStyle& style) noexcept
			{
				return style.m_Space == DebugDrawSpace::World &&
					style.m_DepthMode == DebugDrawDepthMode::Tested;
			});
		fillBatch(m_FrameView.m_OverlayWorld,
			[](const DebugDrawStyle& style) noexcept
			{
				return style.m_Space == DebugDrawSpace::World &&
					style.m_DepthMode == DebugDrawDepthMode::Always;
			});
		fillBatch(m_FrameView.m_OverlayScreen, [](const DebugDrawStyle& style) noexcept
			{ return style.m_Space == DebugDrawSpace::Screen; });
		m_FrameView.m_Statistics.m_LineVertexCount =
			m_FrameView.m_Scene.m_Lines.m_VertexCount +
			m_FrameView.m_OverlayWorld.m_Lines.m_VertexCount +
			m_FrameView.m_OverlayScreen.m_Lines.m_VertexCount;
		m_FrameView.m_Statistics.m_TriangleVertexCount =
			m_FrameView.m_Scene.m_Triangles.m_VertexCount +
			m_FrameView.m_OverlayWorld.m_Triangles.m_VertexCount +
			m_FrameView.m_OverlayScreen.m_Triangles.m_VertexCount;

		m_FrameView.m_VertexBuffer = m_VertexBuffer.Get();
		m_FrameView.m_VertexBufferOffset = m_FrameSlotSizeInBytes * frameSlot;
		if (!m_StagingVertices.empty())
		{
			const size_t sizeInBytes = m_StagingVertices.size() * sizeof(DebugDrawVertex);
			GGLAB_ASSERT(sizeInBytes <= m_FrameSlotSizeInBytes);
			std::memcpy(m_MappedVertices + m_FrameView.m_VertexBufferOffset,
				m_StagingVertices.data(), sizeInBytes);
		}

		return m_FrameView;
	}

	void DebugDrawSystem::Clear() noexcept
	{
		std::scoped_lock lock(m_Mutex);
		m_PendingCommands.clear();
		m_PersistentCommands.clear();
		m_PendingVertexCount = 0;
		m_PersistentVertexCount = 0;
		m_PendingStatistics = {};
	}

	void DebugDrawSystem::ClearChannel(StringID channel) noexcept
	{
		std::scoped_lock lock(m_Mutex);
		auto matches = [channel](const Command& command) noexcept
			{ return command.m_Style.m_Channel == channel; };
		std::erase_if(m_PendingCommands, matches);
		std::erase_if(m_PersistentCommands, matches);
		m_PendingVertexCount = 0;
		m_PersistentVertexCount = 0;
		for (const Command& command : m_PendingCommands)
		{
			m_PendingVertexCount += static_cast<uint32_t>(command.m_Vertices->size());
		}
		for (const Command& command : m_PersistentCommands)
		{
			m_PersistentVertexCount += static_cast<uint32_t>(command.m_Vertices->size());
		}
	}

	void DebugDrawSystem::SetChannelEnabled(StringID channel, bool enabled) noexcept
	{
		std::scoped_lock lock(m_Mutex);
		m_KnownChannels.insert(channel);
		if (enabled)
		{
			m_DisabledChannels.erase(channel);
		}
		else
		{
			m_DisabledChannels.insert(channel);
		}
	}

	bool DebugDrawSystem::IsChannelEnabled(StringID channel) const noexcept
	{
		std::scoped_lock lock(m_Mutex);
		return IsEnabledUnlocked(channel);
	}

	std::vector<DebugDrawChannelState> DebugDrawSystem::GetChannelStates() const noexcept
	{
		std::scoped_lock lock(m_Mutex);
		std::vector<DebugDrawChannelState> states;
		states.reserve(m_KnownChannels.size());
		for (const StringID channel : m_KnownChannels)
		{
			DebugDrawChannelState state{
				.m_Channel = channel,
				.m_Enabled = IsEnabledUnlocked(channel),
			};
			for (const Command& command : m_PendingCommands)
			{
				if (command.m_Style.m_Channel == channel)
				{
					++state.m_PendingCommandCount;
				}
			}
			for (const Command& command : m_PersistentCommands)
			{
				if (command.m_Style.m_Channel == channel)
				{
					++state.m_PersistentCommandCount;
				}
			}
			states.push_back(state);
		}
		std::ranges::sort(states,
			[](const DebugDrawChannelState& lhs, const DebugDrawChannelState& rhs)
			{ return lhs.m_Channel.Value() < rhs.m_Channel.Value(); });
		return states;
	}

	bool DebugDrawSystem::IsEnabledUnlocked(StringID channel) const noexcept
	{
		return !m_DisabledChannels.contains(channel);
	}

	DebugDrawSystem::Command::Bounds DebugDrawSystem::BuildBounds(
		std::span<const Vector3> positions) noexcept
	{
		Command::Bounds bounds{};
		if (positions.empty())
		{
			return bounds;
		}
		bounds.m_Aabb =
			math::CreateAabbFromPoints(positions.size(), positions.data(), sizeof(Vector3));
		bounds.m_Sphere =
			math::CreateSphereFromPoints(positions.size(), positions.data(), sizeof(Vector3));
		bounds.m_Valid = true;
		return bounds;
	}

	bool DebugDrawSystem::ShouldCull(
		const Command& command, const DebugDrawCullContext& cullContext) noexcept
	{
		if (!command.m_Bounds.m_Valid || command.m_Style.m_Space == DebugDrawSpace::Screen)
		{
			return false;
		}
		if (command.m_Style.m_CullingMode == DebugDrawCullingMode::None)
		{
			return false;
		}
		if (command.m_Style.m_CullingMode == DebugDrawCullingMode::Auto)
		{
			for (uint32_t index = 0; index < cullContext.m_DefaultFrustumCount; ++index)
			{
				if (!math::Intersects(
					cullContext.m_DefaultFrustums[index], command.m_Bounds.m_Aabb))
				{
					return true;
				}
			}
			return false;
		}
		if (!cullContext.m_HasMainViewFrustum)
		{
			return false;
		}
		if (command.m_Style.m_CullingMode != DebugDrawCullingMode::MainViewFrustum)
		{
			return false;
		}
		return !math::Intersects(cullContext.m_MainViewFrustum, command.m_Bounds.m_Aabb);
	}

	void DebugDrawSystem::Submit(PrimitiveTopology topology, std::span<const Vector3> positions,
		const DebugDrawStyle& style) noexcept
	{
		std::scoped_lock lock(m_Mutex);
		++m_PendingStatistics.m_SubmittedCommandCount;
		m_KnownChannels.insert(style.m_Channel);
		const bool topologyValid = topology == PrimitiveTopology::Lines ? positions.size() % 2 == 0
			: positions.size() % 3 == 0;
		if (positions.empty() || !topologyValid ||
			!math::IsFinite(style.m_Color) ||
			!std::isfinite(style.m_DurationSeconds) || style.m_DurationSeconds < 0.0f ||
			std::ranges::any_of(
				positions, [](const Vector3& value) noexcept { return !math::IsFinite(value); }))
		{
			++m_PendingStatistics.m_InvalidCommandCount;
			return;
		}
		if (positions.size() > m_MaxVertexCountPerFrame ||
			m_PendingVertexCount + positions.size() > m_MaxVertexCountPerFrame)
		{
			++m_PendingStatistics.m_DroppedCommandCount;
			if (!m_BudgetWarningEmitted)
			{
				m_BudgetWarningEmitted = true;
				GGLAB_LOG_GRAPHICS_WARN("DebugDraw vertex budget exceeded; commands were dropped.");
			}
			return;
		}

		auto vertices = std::make_shared<std::vector<DebugDrawVertex>>();
		vertices->reserve(positions.size());
		for (const Vector3& position : positions)
		{
			vertices->push_back({ position, style.m_Color });
		}
		m_PendingCommands.push_back(
			{ topology, style, std::move(vertices), BuildBounds(positions), 0.0f });
		m_PendingVertexCount += static_cast<uint32_t>(positions.size());
		++m_PendingStatistics.m_AcceptedCommandCount;
	}

	void DebugDrawSystem::RejectInvalid() noexcept
	{
		std::scoped_lock lock(m_Mutex);
		++m_PendingStatistics.m_SubmittedCommandCount;
		++m_PendingStatistics.m_InvalidCommandCount;
	}

	void DebugDrawContext::Line(
		const Vector3& start, const Vector3& end, const DebugDrawStyle& style) noexcept
	{
		if (m_System)
		{
			const std::array positions{ start, end };
			m_System->Submit(DebugDrawSystem::PrimitiveTopology::Lines, positions, style);
		}
	}

	void DebugDrawContext::Polyline(
		std::span<const Vector3> points, bool closed, const DebugDrawStyle& style) noexcept
	{
		if (!m_System || points.size() < 2)
		{
			return;
		}
		std::vector<Vector3> positions;
		positions.reserve((points.size() - 1 + (closed ? 1 : 0)) * 2);
		for (size_t index = 1; index < points.size(); ++index)
		{
			positions.push_back(points[index - 1]);
			positions.push_back(points[index]);
		}
		if (closed)
		{
			positions.push_back(points.back());
			positions.push_back(points.front());
		}
		m_System->Submit(DebugDrawSystem::PrimitiveTopology::Lines, positions, style);
	}

	void DebugDrawContext::Point(
		const Vector3& position, float size, const DebugDrawStyle& style) noexcept
	{
		if (!m_System || !std::isfinite(size) || size <= 0.0f)
		{
			if (m_System)
				m_System->RejectInvalid();
			return;
		}
		const Vector3 x(size, 0.0f, 0.0f);
		const Vector3 y(0.0f, size, 0.0f);
		if (style.m_Space == DebugDrawSpace::Screen)
		{
			const std::array positions = {
				position - x,
				position + x,
				position - y,
				position + y,
			};
			m_System->Submit(DebugDrawSystem::PrimitiveTopology::Lines, positions, style);
			return;
		}

		const Vector3 z(0.0f, 0.0f, size);
		const std::array positions = {
			position - x,
			position + x,
			position - y,
			position + y,
			position - z,
			position + z,
		};
		m_System->Submit(DebugDrawSystem::PrimitiveTopology::Lines, positions, style);
	}

	void DebugDrawContext::Arrow(const Vector3& start, const Vector3& end, float headLength,
		const DebugDrawStyle& style, uint32_t segments) noexcept
	{
		if (!m_System || !std::isfinite(headLength) || headLength <= 0.0f)
		{
			if (m_System)
				m_System->RejectInvalid();
			return;
		}
		Vector3 direction = end - start;
		const float length = direction.Length();
		if (!std::isfinite(length) || length <= 1.0e-6f)
		{
			m_System->RejectInvalid();
			return;
		}
		direction /= length;
		headLength = std::min(headLength, length);
		const Vector3 baseCenter = end - direction * headLength;
		Line(start, baseCenter, style);
		Cone(end, -direction, headLength, headLength * 0.45f, style, segments);
	}

	void DebugDrawContext::Axes(const Matrix& transform, float length, float headLength,
		const DebugDrawStyle& style) noexcept
	{
		if (!m_System ||
			!std::isfinite(length) || !std::isfinite(headLength) ||
			length <= 0.0f || headLength <= 0.0f)
		{
			if (m_System)
				m_System->RejectInvalid();
			return;
		}
		const Vector3 origin = math::TransformPoint(Vector3::Zero, transform);
		DebugDrawStyle axisStyle = style;
		axisStyle.m_Color = Color::Red;
		Arrow(origin, math::TransformPoint(Vector3::UnitX * length, transform), headLength,
			axisStyle);
		axisStyle.m_Color = Color::Green;
		Arrow(origin, math::TransformPoint(Vector3::UnitY * length, transform), headLength,
			axisStyle);
		axisStyle.m_Color = Color::Blue;
		Arrow(origin, math::TransformPoint(Vector3::UnitZ * length, transform), headLength,
			axisStyle);
	}

	void DebugDrawContext::Aabb(const math::Aabb& bounds, const DebugDrawStyle& style) noexcept
	{
		Box(bounds.m_Center, bounds.m_Extents, style);
	}
}
