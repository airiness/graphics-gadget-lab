#include "Precompiled.h"
#include "DX12CommandAllocator.h"
#include "DX12Device.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	// DX12CommandAllocator
	DX12CommandAllocator::DX12CommandAllocator(DX12Device* dx12Device, D3D12_COMMAND_LIST_TYPE type) noexcept
	{
		CreateCommandAllocator(dx12Device, type);
	}

	void DX12CommandAllocator::CreateCommandAllocator(DX12Device* dx12Device, D3D12_COMMAND_LIST_TYPE type) noexcept
	{
		auto device = dx12Device->Get();
		utility::ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&m_D3D12CommandAllocator)));

#if defined (BUILD_DEBUG)
		utility::SetDebugName(m_D3D12CommandAllocator.Get(),
			std::format(L"CommandAllocator[{:p}]_{} ", (void*)this, utility::GetCommandListTypeName(type)).c_str());
#endif
	}


	// DX12CommandAllocatorPool
	DX12CommandAllocatorPool::DX12CommandAllocatorPool(DX12Device* dx12Device) noexcept :
		m_DX12Device(dx12Device)
	{
	}
	DX12CommandAllocatorPool::~DX12CommandAllocatorPool() noexcept
	{
	}


}

