#include "Precompiled.h"
#include "DX12CommandQueue.h"
#include "DX12CommandList.h"
#include "DX12Device.h"
#include "DX12Fence.h"
#include "HResult.h"

namespace gglab
{
	DX12CommandQueue::DX12CommandQueue(const CreateInfo& createInfo) noexcept :
		m_DX12Device(createInfo.m_DX12Device),
		m_Fence(std::make_unique<DX12Fence>(m_DX12Device))
	{
		GGLAB_ASSERT(m_DX12Device);

		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = createInfo.m_Type;
		desc.Priority = createInfo.m_Priority;
		desc.Flags = createInfo.m_Flags;
		desc.NodeMask = 0;

		GGLAB_HR_DX(
			m_DX12Device->Get()->CreateCommandQueue(
				&desc, IID_PPV_ARGS(&m_D3D12CommandQueue)),
			m_DX12Device->Get());
	}

	DX12CommandQueue::~DX12CommandQueue() = default;

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

	void DX12CommandQueue::Wait(const DX12FencePoint& fencePoint) const noexcept
	{
		if (!fencePoint.IsValid())
		{
			return;
		}

		auto* fence = fencePoint.GetFence()->Get();
		auto value = fencePoint.GetValue();

		m_D3D12CommandQueue->Wait(fence, value);
	}
}