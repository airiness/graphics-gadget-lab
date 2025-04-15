#include "Precompiled.h"
#include "DX12Resource.h"
#include "DX12Device.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	DX12Resource::DX12Resource(DX12Device* device, 
		D3D12_HEAP_TYPE heapType, 
		const CD3DX12_RESOURCE_DESC& resourceDesc, 
		D3D12_RESOURCE_STATES initState, 
		std::optional<D3D12_CLEAR_VALUE> clearValue) noexcept :
		m_DX12Deivce(device),
		m_ResourceDesc(resourceDesc),
		m_ResourceState(initState),
		m_ClearValue(clearValue)
	{
		CreateAllocation(heapType);
	}
	ID3D12Resource* DX12Resource::Get() const
	{
		return m_D3D12MAAllocation->GetResource();
	}

	void DX12Resource::CreateAllocation(D3D12_HEAP_TYPE heapType)
	{
		auto allocator = m_DX12Deivce->GetMemAllocator();

		D3D12MA::ALLOCATION_DESC allocationDesc = {};
		allocationDesc.CustomPool = nullptr;
		allocationDesc.ExtraHeapFlags = D3D12_HEAP_FLAG_NONE;
		allocationDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
		allocationDesc.HeapType = heapType;
		allocationDesc.pPrivateData = nullptr;

		utility::ThrowIfFailed(allocator->CreateResource(
			&allocationDesc,
			&m_ResourceDesc,
			m_ResourceState,
			m_ClearValue.has_value() ? &m_ClearValue.value() : nullptr,
			&m_D3D12MAAllocation,
			IID_NULL, nullptr));

	}
}


