#pragma once
#include "Graphics/DebugDraw/DebugDraw.h"
#include "Graphics/RHI/RHIResource.h"

#include <mutex>
#include <vector>

namespace gglab
{
	class RHIDevice;

	class DebugDrawSystem
	{
	public:
		static constexpr uint32_t DefaultMaxLineCount = 65'536;

		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			uint32_t m_FrameSlotCount = 0;
			uint32_t m_MaxLineCountPerFrame = DefaultMaxLineCount;
		};

		explicit DebugDrawSystem(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DebugDrawSystem);
		~DebugDrawSystem() noexcept;

		DebugDrawContext& GetContext() noexcept { return m_Context; }
		const DebugDrawFrameView& SealFrame(uint32_t frameSlot) noexcept;
		const DebugDrawFrameView& GetFrameView() const noexcept { return m_FrameView; }
		void Clear() noexcept;

	private:
		friend class DebugDrawContext;

		struct LineCommand
		{
			Vector3 m_Start{};
			Vector3 m_End{};
			DebugDrawStyle m_Style{};
		};

		void Submit(std::span<const LineCommand> lines) noexcept;
		[[nodiscard]] bool IsValid(const LineCommand& line) const noexcept;

		RHIDevice* m_Device = nullptr;
		RHIBufferOwner m_VertexBuffer{};
		std::byte* m_MappedVertices = nullptr;
		DebugDrawContext m_Context;

		std::mutex m_Mutex;
		std::vector<LineCommand> m_PendingLines;
		std::vector<LineCommand> m_SealedLines;
		std::vector<DebugDrawVertex> m_StagingVertices;
		DebugDrawFrameView m_FrameView{};

		uint64_t m_FrameSlotSizeInBytes = 0;
		uint64_t m_TotalBufferSizeInBytes = 0;
		uint32_t m_FrameSlotCount = 0;
		uint32_t m_MaxLineCountPerFrame = 0;
		bool m_BudgetWarningEmitted = false;
	};
}
