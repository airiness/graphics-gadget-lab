#include "Precompiled.h"
#include "DX12Resource.h"
#include "DX12Device.h"
#include "Utility.h"
#include <D3D12MemAlloc.h>

namespace graphicsGadgetLab
{
	ID3D12Resource* DX12Resource::Get() const
	{
		return m_D3D12MAAllocation->GetResource();
	}

	void DX12Resource::CreateAllocation()
	{
		auto allocator = m_DX12Deivce->GetMemAllocator();

		D3D12MA::ALLOCATION_DESC allocationDesc = {};
		allocationDesc.CustomPool = nullptr;
		allocationDesc.ExtraHeapFlags = D3D12_HEAP_FLAG_NONE;
		allocationDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
		allocationDesc.HeapType = ; //TODO:heap
		allocationDesc.pPrivateData = nullptr;

		D3D12_RESOURCE_DESC desc;
		CD3DX12_RESOURCE_DESC desc;

		utility::ThrowIfFailed(allocator->CreateResource(&allocationDesc, )

	}
}


