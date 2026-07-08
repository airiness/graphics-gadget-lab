#pragma once
#include "Graphics/DebugDraw/DebugDraw.h"
#include "Graphics/RHI/RHIResource.h"

#include <mutex>
#include <memory>
#include <unordered_set>
#include <vector>

namespace gglab
{
	class RHIDevice;

	class DebugDrawSystem
	{
	public:
		static constexpr uint32_t DefaultMaxVertexCount = 131'072;

		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			uint32_t m_FrameSlotCount = 0;
			uint32_t m_MaxVertexCountPerFrame = DefaultMaxVertexCount;
		};

		explicit DebugDrawSystem(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DebugDrawSystem);
		~DebugDrawSystem() noexcept;

		DebugDrawContext& GetContext() noexcept { return m_Context; }
		const DebugDrawFrameView& SealFrame(
			uint32_t frameSlot,
			float deltaTime,
			const DebugDrawCullContext& cullContext) noexcept;
		void Clear() noexcept;
		void ClearChannel(StringID channel) noexcept;
		void SetChannelEnabled(StringID channel, bool enabled) noexcept;
		[[nodiscard]] bool IsChannelEnabled(StringID channel) const noexcept;
		[[nodiscard]] std::vector<DebugDrawChannelState> GetChannelStates() const noexcept;

	private:
		friend class DebugDrawContext;

		enum class PrimitiveTopology : uint8_t
		{
			Lines,
			Triangles,
		};

		struct Command
		{
			struct Bounds
			{
				math::Aabb m_Aabb{};
				math::Sphere m_Sphere{};
				bool m_Valid = false;
			};

			PrimitiveTopology m_Topology = PrimitiveTopology::Lines;
			DebugDrawStyle m_Style{};
			std::shared_ptr<const std::vector<DebugDrawVertex>> m_Vertices;
			Bounds m_Bounds{};
			float m_RemainingSeconds = 0.0f;
		};

		void Submit(PrimitiveTopology topology,
			std::span<const Vector3> positions,
			const DebugDrawStyle& style) noexcept;
		void RejectInvalid() noexcept;
		[[nodiscard]] bool IsEnabledUnlocked(StringID channel) const noexcept;
		[[nodiscard]] static Command::Bounds BuildBounds(
			std::span<const Vector3> positions) noexcept;
		[[nodiscard]] static bool ShouldCull(
			const Command& command,
			const DebugDrawCullContext& cullContext) noexcept;

		RHIDevice* m_Device = nullptr;
		RHIBufferOwner m_VertexBuffer{};
		std::byte* m_MappedVertices = nullptr;
		DebugDrawContext m_Context;

		mutable std::mutex m_Mutex;
		std::vector<Command> m_PendingCommands;
		std::vector<Command> m_SealedCommands;
		std::vector<Command> m_PersistentCommands;
		std::unordered_set<StringID> m_KnownChannels;
		std::unordered_set<StringID> m_DisabledChannels;
		std::vector<DebugDrawVertex> m_StagingVertices;
		DebugDrawFrameView m_FrameView{};
		DebugDrawStatistics m_PendingStatistics{};

		uint64_t m_FrameSlotSizeInBytes = 0;
		uint64_t m_TotalBufferSizeInBytes = 0;
		uint32_t m_FrameSlotCount = 0;
		uint32_t m_MaxVertexCountPerFrame = 0;
		uint32_t m_PendingVertexCount = 0;
		uint32_t m_PersistentVertexCount = 0;
		bool m_BudgetWarningEmitted = false;
	};
}
