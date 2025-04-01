#include "Precompiled.h"
#include "DX12CommandQueue.h"
#include "Utility.h"
#include "Application.h"

namespace graphicsGadgetLab
{
	DX12CommandQueue::DX12CommandQueue(D3D12_COMMAND_LIST_TYPE type, int32_t priority, D3D12_COMMAND_QUEUE_FLAGS flags) noexcept:
		m_D3D12CommandQueue(CreateCommandQueue(type, priority, flags))
	{	
	}

	ComPtr<ID3D12CommandQueue> DX12CommandQueue::CreateCommandQueue(D3D12_COMMAND_LIST_TYPE type, int32_t priority, D3D12_COMMAND_QUEUE_FLAGS flags) const noexcept
	{
		auto* device = Application::Get()->GetRenderer()->GetDevice().Get();

		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = type;
		desc.Priority = priority;
		desc.Flags = flags;
		desc.NodeMask = 0;

		ComPtr<ID3D12CommandQueue> commandQueue;
		utility::ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue)));

		return commandQueue;
	}
}