#include "Precompiled.h"
#include "DX12CommandQueue.h"
#include "DX12CommandList.h"
#include "DX12Device.h"
#include "DX12Fence.h"

namespace gglab
{
	DX12CommandQueue::DX12CommandQueue(
		DX12Device* dx12Device,
		D3D12_COMMAND_LIST_TYPE type, 
		int32_t priority, 
		D3D12_COMMAND_QUEUE_FLAGS flags) noexcept :
		m_DX12Device(dx12Device),
		m_D3D12CommandQueue(m_DX12Device->CreateDirectX12CommandQueue(type, priority, flags)),
		m_Fence(std::make_unique<DX12Fence>(m_DX12Device))
	{	
	}

	DX12FencePoint DX12CommandQueue::Execute(std::span<const DX12CommandList* const> commandLists) noexcept
	{
		std::vector<ID3D12CommandList*> d3d12CommandLists;
		d3d12CommandLists.reserve(commandLists.size());

		for (const auto& commandList : commandLists)
		{
			d3d12CommandLists.push_back(commandList->Get());
		}

		m_D3D12CommandQueue->ExecuteCommandLists(static_cast<UINT>(d3d12CommandLists.size()), d3d12CommandLists.data());

		return Signal();
	}

	void DX12CommandQueue::FlushCommandQueue() noexcept
	{
		auto fencePoint = Signal();
		fencePoint.Wait();
	}

	DX12FencePoint DX12CommandQueue::Signal() noexcept
	{
		return m_Fence->Signal(this);
	}

	void DX12CommandQueue::Wait(const DX12FencePoint& fencePoint) noexcept
	{
		if(!fencePoint.IsValid())
		{
			return;
		}

		auto* fence = fencePoint.GetFence()->Get();
		auto value = fencePoint.GetValue();

		m_D3D12CommandQueue->Wait(fence, value);
	}
}