#include "Precompiled.h"
#include "DX12CommandAllocator.h"
#include "DX12Device.h"
#include "HResult.h"

namespace gglab
{
	// DX12CommandAllocator
	DX12CommandAllocator::DX12CommandAllocator(const CreateInfo& createInfo) noexcept
	{
		GGLAB_ASSERT(createInfo.m_DX12Device);

		auto* device = createInfo.m_DX12Device->Get();
		GGLAB_HR_DX(device->CreateCommandAllocator(createInfo.m_Type, IID_PPV_ARGS(&m_D3D12CommandAllocator)), device);
	}

	// DX12CommandAllocatorPool
	DX12CommandAllocatorPool::DX12CommandAllocatorPool(const DX12CommandAllocator::CreateInfo& createInfo) noexcept :
		m_DX12Device(createInfo.m_DX12Device),
		m_Type(createInfo.m_Type)
	{
	}

	DX12CommandAllocator* DX12CommandAllocatorPool::RequestCommandAllocator() noexcept
	{
		std::lock_guard lock(m_Mutex);

		if (!mRecycledAllocators.empty())
		{
			const auto& allocatorPair = mRecycledAllocators.front();
			if (allocatorPair.second.IsCompleted())
			{
				mRecycledAllocators.pop();
				return allocatorPair.first;
			}
		}

		DX12CommandAllocator::CreateInfo createInfo
		{
			.m_DX12Device = m_DX12Device,
			.m_Type = m_Type
		};
		auto newAllocator = std::make_unique<DX12CommandAllocator>(createInfo);
		auto allocatorPtr = newAllocator.get();
		m_Pool.push_back(std::move(newAllocator));

		return allocatorPtr;
	}

	void DX12CommandAllocatorPool::RecycleCommandAllocator(DX12CommandAllocator* allocator, DX12FencePoint fencePoint) noexcept
	{
		std::lock_guard lock(m_Mutex);
		mRecycledAllocators.emplace(allocator, fencePoint);
	}
}