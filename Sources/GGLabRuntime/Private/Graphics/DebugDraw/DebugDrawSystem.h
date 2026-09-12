#pragma once
#include "GGLabRuntime/Graphics/DebugDraw/DebugDrawService.h"
#include "GGLabRuntime/Graphics/RHI/RHIResource.h"

#include <mutex>
#include <memory>
#include <unordered_set>
#include <vector>

namespace gglab
{
	class RHIDevice;

	class DebugDrawSystem final :
		public DebugDrawService,
		public DebugDrawChannelViewBase,
		public DebugDrawChannelControlBase
	{
	public:
		explicit DebugDrawSystem(const DebugDrawServiceCreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DebugDrawSystem);
		~DebugDrawSystem() noexcept override;

		[[nodiscard]] DebugDrawContext& GetContext() noexcept override { return m_Context; }
		[[nodiscard]] const DebugDrawFrameView& SealFrame(uint32_t frameSlot,
			float deltaTime, const DebugDrawCullContext& cullContext) noexcept override;
		void Clear() noexcept override;
		void ClearChannel(StringID channel) noexcept override;
		void SetChannelEnabled(StringID channel, bool enabled) noexcept override;
		[[nodiscard]] bool IsChannelEnabled(StringID channel) const noexcept;
		[[nodiscard]] std::vector<DebugDrawChannelState> GetChannelStates()
			const noexcept override;
		[[nodiscard]] DebugDrawChannelViewBase* GetChannelView() noexcept override
		{
			return this;
		}
		[[nodiscard]] DebugDrawChannelControlBase* GetChannelControl() noexcept override
		{
			return this;
		}

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

		void Submit(PrimitiveTopology topology, std::span<const Vector3> positions,
			const DebugDrawStyle& style) noexcept;
		void RejectInvalid() noexcept;
		[[nodiscard]] bool IsEnabledUnlocked(StringID channel) const noexcept;
		[[nodiscard]] static Command::Bounds BuildBounds(
			std::span<const Vector3> positions) noexcept;
		[[nodiscard]] static bool ShouldCull(
			const Command& command, const DebugDrawCullContext& cullContext) noexcept;

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
