#include "Precompiled.h"
#include "DX12CommandQueue.h"
#include "Application.h"
#include "DX12Device.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	DX12CommandQueue::DX12CommandQueue(
		DX12Device* dx12Device,
		D3D12_COMMAND_LIST_TYPE type, 
		int32_t priority, 
		D3D12_COMMAND_QUEUE_FLAGS flags) noexcept :
		m_DX12Device(dx12Device),
		m_D3D12CommandQueue(CreateCommandQueue(type, priority, flags))
	{	
	}

	ComPtr<ID3D12CommandQueue> DX12CommandQueue::CreateCommandQueue(
		D3D12_COMMAND_LIST_TYPE type, 
		int32_t priority, 
		D3D12_COMMAND_QUEUE_FLAGS flags) const noexcept
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = type;
		desc.Priority = priority;
		desc.Flags = flags;
		desc.NodeMask = 0;

		ComPtr<ID3D12CommandQueue> commandQueue;
		utility::ThrowIfFailed(m_DX12Device->Get()->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue)));

#if defined (BUILD_DEBUG)
		utility::SetDebugName(commandQueue.Get(), 
			std::format(L"CommandQueue[{:p}]_{} ", (void*)this, utility::GetCommandListTypeName(type)).c_str());
#endif

		return commandQueue;
	}
}