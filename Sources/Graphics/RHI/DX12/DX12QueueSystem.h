#pragma once
#include "Core/CoreMacros.h"
#include "Core/Utility/TypeUtils.h"
#include "Graphics/RHI/RHIFence.h"

#include <array>
#include <memory>
#include <vector>

namespace gglab
{
	enum class DX12QueueType : uint8_t
	{
		Graphics,
		Compute,
		Copy,
		Transfer,

		Count,
	};

	class DX12CommandAllocatorPool;
	class DX12CommandList;
	class DX12CommandQueue;
	class DX12ComputeCommandContext;
	class DX12Device;
	class DX12Fence;
	class DX12GraphicsCommandContext;

	class DX12QueueSystem final
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_Device = nullptr;
			uint32_t m_FrameCount = 2;
		};

		explicit DX12QueueSystem(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12QueueSystem);
		~DX12QueueSystem();

		void Finalize() noexcept;
		[[nodiscard]] bool IsValid() const noexcept { return m_Device != nullptr; }

		[[nodiscard]] uint32_t GetFrameCount() const noexcept { return m_FrameCount; }
		[[nodiscard]] DX12CommandQueue& GetQueue(DX12QueueType type) noexcept;
		[[nodiscard]] const DX12CommandQueue& GetQueue(DX12QueueType type) const noexcept;
		[[nodiscard]] DX12CommandAllocatorPool& GetAllocatorPool(DX12QueueType type) noexcept;
		[[nodiscard]] DX12CommandList& GetGraphicsCommandList(uint32_t frameIndex) noexcept;
		[[nodiscard]] DX12CommandList& GetComputeCommandList(uint32_t frameIndex) noexcept;
		[[nodiscard]] DX12GraphicsCommandContext& GetGraphicsContext(uint32_t frameIndex) noexcept;
		[[nodiscard]] DX12ComputeCommandContext& GetComputeContext(uint32_t frameIndex) noexcept;
		[[nodiscard]] std::unique_ptr<DX12CommandList> CreateCommandList(
			DX12QueueType type) const noexcept;

		void WaitForFence(RHIQueueType waitingQueue,
			const RHIFencePoint& fencePoint) noexcept;
		void WaitForFenceCompletion(const RHIFencePoint& fencePoint) noexcept;
		[[nodiscard]] bool IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept;
		[[nodiscard]] const DX12Fence* ResolveFence(RHIFenceHandle fence) const noexcept;
		void WaitIdle() noexcept;

		[[nodiscard]] static DX12QueueType ToDX12QueueType(RHIQueueType type) noexcept;

	private:
		void InitializeQueues() noexcept;
		void InitializeAllocatorPools() noexcept;
		void InitializeFrameContexts() noexcept;

		DX12Device* m_Device = nullptr;
		uint32_t m_FrameCount = 0;
		std::array<std::unique_ptr<DX12CommandQueue>, utils::EnumCount<DX12QueueType>()> m_Queues;
		std::array<std::unique_ptr<DX12CommandAllocatorPool>, utils::EnumCount<DX12QueueType>()> m_AllocatorPools;
		std::vector<std::unique_ptr<DX12CommandList>> m_GraphicsCommandLists;
		std::vector<std::unique_ptr<DX12CommandList>> m_ComputeCommandLists;
		std::vector<std::unique_ptr<DX12GraphicsCommandContext>> m_GraphicsContexts;
		std::vector<std::unique_ptr<DX12ComputeCommandContext>> m_ComputeContexts;
	};
}
