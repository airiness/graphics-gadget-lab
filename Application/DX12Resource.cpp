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

	DX12Resource::DX12Resource(DX12Device* device, 
		ComPtr<ID3D12Resource> resource,
		D3D12_RESOURCE_STATES resourceState,
		std::optional<D3D12_CLEAR_VALUE> clearValue = std::nullopt) noexcept :
		m_DX12Deivce(device),
		m_Resource(resource),
		m_ResourceState(resourceState),
		m_ClearValue(clearValue)
	{
		m_ResourceDesc = CD3DX12_RESOURCE_DESC(m_Resource->GetDesc());
	}

	ID3D12Resource* DX12Resource::Get() const
	{
		return m_Resource.Get();
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


		ComPtr<D3D12MA::Allocation> allocation;
		utility::ThrowIfFailed(allocator->CreateResource(
			&allocationDesc,
			&m_ResourceDesc,
			m_ResourceState,
			m_ClearValue.has_value() ? &m_ClearValue.value() : nullptr,
			&allocation,
			IID_PPV_ARGS(&m_Resource)));
	}
}


