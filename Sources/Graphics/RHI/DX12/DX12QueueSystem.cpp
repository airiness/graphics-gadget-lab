#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12QueueSystem.h"
#include "Graphics/RHI/DX12/DX12CommandAllocator.h"
#include "Graphics/RHI/DX12/DX12CommandContext.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12Fence.h"

namespace gglab
{
	DX12QueueSystem::DX12QueueSystem(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_FrameCount(createInfo.m_FrameCount)
	{
		GGLAB_ASSERT_NOT_NULL(m_Device);
		GGLAB_ASSERT_MSG(m_FrameCount > 0, "DX12QueueSystem requires at least one frame slot.");
		InitializeQueues();
		InitializeAllocatorPools();
		InitializeFrameContexts();
	}

	DX12QueueSystem::~DX12QueueSystem()
	{
		Finalize();
	}

	void DX12QueueSystem::Finalize() noexcept
	{
		if (!m_Device)
		{
			return;
		}

		WaitIdle();
		m_GraphicsContexts.clear();
		m_ComputeContexts.clear();
		m_GraphicsCommandLists.clear();
		m_ComputeCommandLists.clear();
		m_AllocatorPools = {};
		m_Queues = {};
		m_FrameCount = 0;
		m_Device = nullptr;
	}

	DX12CommandQueue& DX12QueueSystem::GetQueue(DX12QueueType type) noexcept
	{
		auto& queue = m_Queues[utils::ToIndexChecked(type)];
		GGLAB_ASSERT_NOT_NULL(queue.get());
		return *queue;
	}

	const DX12CommandQueue& DX12QueueSystem::GetQueue(DX12QueueType type) const noexcept
	{
		const auto& queue = m_Queues[utils::ToIndexChecked(type)];
		GGLAB_ASSERT_NOT_NULL(queue.get());
		return *queue;
	}

	DX12CommandAllocatorPool& DX12QueueSystem::GetAllocatorPool(DX12QueueType type) noexcept
	{
		auto& pool = m_AllocatorPools[utils::ToIndexChecked(type)];
		GGLAB_ASSERT_NOT_NULL(pool.get());
		return *pool;
	}

	DX12CommandList& DX12QueueSystem::GetGraphicsCommandList(uint32_t frameIndex) noexcept
	{
		return *m_GraphicsCommandLists.at(frameIndex);
	}

	DX12CommandList& DX12QueueSystem::GetComputeCommandList(uint32_t frameIndex) noexcept
	{
		return *m_ComputeCommandLists.at(frameIndex);
	}

	DX12GraphicsCommandContext& DX12QueueSystem::GetGraphicsContext(uint32_t frameIndex) noexcept
	{
		return *m_GraphicsContexts.at(frameIndex);
	}

	DX12ComputeCommandContext& DX12QueueSystem::GetComputeContext(uint32_t frameIndex) noexcept
	{
		return *m_ComputeContexts.at(frameIndex);
	}

	std::unique_ptr<DX12CommandList> DX12QueueSystem::CreateCommandList(
		DX12QueueType type) const noexcept
	{
		DX12CommandList::CreateInfo createInfo{};
		createInfo.m_DX12Device = m_Device;
		switch (type)
		{
		case DX12QueueType::Graphics:
		case DX12QueueType::Transfer:
			createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			break;
		case DX12QueueType::Compute:
			createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
			break;
		case DX12QueueType::Copy:
			createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_COPY;
			break;
		default:
			GGLAB_UNREACHABLE("Unhandled DX12QueueType.");
		}
		return std::make_unique<DX12CommandList>(createInfo);
	}

	void DX12QueueSystem::WaitForFence(
		RHIQueueType waitingQueue,
		const RHIFencePoint& fencePoint) noexcept
	{
		if (!fencePoint.IsValid())
		{
			return;
		}

		const DX12Fence* fence = ResolveFence(fencePoint.m_Fence);
		if (!fence)
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12QueueSystem::WaitForFence received an unknown fence.");
			return;
		}
		GetQueue(ToDX12QueueType(waitingQueue)).Wait(*fence, fencePoint.m_Value);
	}

	void DX12QueueSystem::WaitForFenceCompletion(const RHIFencePoint& fencePoint) noexcept
	{
		if (!fencePoint.IsValid())
		{
			return;
		}
		if (const DX12Fence* fence = ResolveFence(fencePoint.m_Fence))
		{
			fence->WaitCompletion(fencePoint.m_Value);
			return;
		}
		GGLAB_LOG_GRAPHICS_WARN(
			"DX12QueueSystem::WaitForFenceCompletion received an unknown fence.");
	}

	bool DX12QueueSystem::IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept
	{
		if (!fencePoint.IsValid())
		{
			return true;
		}
		if (const DX12Fence* fence = ResolveFence(fencePoint.m_Fence))
		{
			return fence->IsCompleted(fencePoint.m_Value);
		}
		GGLAB_LOG_GRAPHICS_WARN(
			"DX12QueueSystem::IsFencePointCompleted received an unknown fence.");
		return false;
	}

	const DX12Fence* DX12QueueSystem::ResolveFence(RHIFenceHandle fenceHandle) const noexcept
	{
		for (const auto& queue : m_Queues)
		{
			if (!queue)
			{
				continue;
			}
			const DX12Fence* fence = queue->GetFence();
			if (fence && fence->GetRHIHandle() == fenceHandle)
			{
				return fence;
			}
		}
		return nullptr;
	}

	void DX12QueueSystem::WaitIdle() noexcept
	{
		for (auto& queue : m_Queues)
		{
			if (queue)
			{
				queue->FlushCommandQueue();
			}
		}
	}

	DX12QueueType DX12QueueSystem::ToDX12QueueType(RHIQueueType type) noexcept
	{
		switch (type)
		{
		case RHIQueueType::Graphics: return DX12QueueType::Graphics;
		case RHIQueueType::Compute: return DX12QueueType::Compute;
		case RHIQueueType::Copy: return DX12QueueType::Copy;
		case RHIQueueType::Transfer: return DX12QueueType::Transfer;
		}
		GGLAB_UNREACHABLE("Unhandled RHIQueueType.");
	}

	void DX12QueueSystem::InitializeQueues() noexcept
	{
		DX12CommandQueue::CreateInfo createInfo{};
		createInfo.m_DX12Device = m_Device;

		createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		m_Queues[utils::ToIndex(DX12QueueType::Graphics)] =
			std::make_unique<DX12CommandQueue>(createInfo);
		m_Queues[utils::ToIndex(DX12QueueType::Transfer)] =
			std::make_unique<DX12CommandQueue>(createInfo);

		createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		m_Queues[utils::ToIndex(DX12QueueType::Compute)] =
			std::make_unique<DX12CommandQueue>(createInfo);

		createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_COPY;
		m_Queues[utils::ToIndex(DX12QueueType::Copy)] =
			std::make_unique<DX12CommandQueue>(createInfo);
	}

	void DX12QueueSystem::InitializeAllocatorPools() noexcept
	{
		DX12CommandAllocator::CreateInfo createInfo{};
		createInfo.m_DX12Device = m_Device;

		createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		m_AllocatorPools[utils::ToIndex(DX12QueueType::Graphics)] =
			std::make_unique<DX12CommandAllocatorPool>(createInfo);
		m_AllocatorPools[utils::ToIndex(DX12QueueType::Transfer)] =
			std::make_unique<DX12CommandAllocatorPool>(createInfo);

		createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		m_AllocatorPools[utils::ToIndex(DX12QueueType::Compute)] =
			std::make_unique<DX12CommandAllocatorPool>(createInfo);

		createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_COPY;
		m_AllocatorPools[utils::ToIndex(DX12QueueType::Copy)] =
			std::make_unique<DX12CommandAllocatorPool>(createInfo);
	}

	void DX12QueueSystem::InitializeFrameContexts() noexcept
	{
		m_GraphicsCommandLists.reserve(m_FrameCount);
		m_ComputeCommandLists.reserve(m_FrameCount);
		m_GraphicsContexts.reserve(m_FrameCount);
		m_ComputeContexts.reserve(m_FrameCount);

		for (uint32_t i = 0; i < m_FrameCount; ++i)
		{
			auto graphicsList = CreateCommandList(DX12QueueType::Graphics);
			m_GraphicsContexts.push_back(
				std::make_unique<DX12GraphicsCommandContext>(m_Device, graphicsList.get()));
			m_GraphicsCommandLists.push_back(std::move(graphicsList));

			auto computeList = CreateCommandList(DX12QueueType::Compute);
			m_ComputeContexts.push_back(
				std::make_unique<DX12ComputeCommandContext>(m_Device, computeList.get()));
			m_ComputeCommandLists.push_back(std::move(computeList));
		}
	}
}
