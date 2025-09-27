#include "Precompiled.h"
#include "DX12CommandAllocator.h"
#include "DX12Device.h"
#include "HResult.h"

namespace gglab
{
	// DX12CommandAllocator
	DX12CommandAllocator::DX12CommandAllocator(DX12Device* dx12Device, D3D12_COMMAND_LIST_TYPE type) noexcept
	{
		CreateCommandAllocator(dx12Device, type);
	}

	void DX12CommandAllocator::CreateCommandAllocator(DX12Device* dx12Device, D3D12_COMMAND_LIST_TYPE type) noexcept
	{
		auto* device = dx12Device->Get();
		GGLAB_HR(device->CreateCommandAllocator(type, IID_PPV_ARGS(&m_D3D12CommandAllocator)), device);
	}

	// DX12CommandAllocatorPool
	DX12CommandAllocatorPool::DX12CommandAllocatorPool(DX12Device* dx12Device, D3D12_COMMAND_LIST_TYPE type) noexcept :
		m_DX12Device(dx12Device),
		m_Type(type)
	{
	}

	DX12CommandAllocatorPool::~DX12CommandAllocatorPool() noexcept
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

		auto newAllocator = std::make_unique<DX12CommandAllocator>(m_DX12Device, m_Type);
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

