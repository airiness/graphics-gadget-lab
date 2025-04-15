#include "Precompiled.h"
#include "DX12Buffer.h"

namespace graphicsGadgetLab
{
	DX12Buffer::DX12Buffer(DX12Device* device,
		D3D12_HEAP_TYPE heapType,
		const CD3DX12_RESOURCE_DESC& resourceDesc,
		D3D12_RESOURCE_STATES initState,
		std::optional<D3D12_CLEAR_VALUE> clearValue) noexcept :
		DX12Resource(device, heapType, resourceDesc, initState, clearValue)
	{
	}

	DX12Buffer::~DX12Buffer() noexcept
	{

	}
}
