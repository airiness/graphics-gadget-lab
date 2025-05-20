#include "Precompiled.h"
#include "DX12Buffer.h"
#include "Utility.h"

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

	void* DX12Buffer::Map()
	{
		void* memory = nullptr;
		utility::ThrowIfFailed(Get()->Map(0, nullptr, &memory));
		return memory;
	}

	void DX12Buffer::UnMap()
	{
		Get()->Unmap(0, nullptr);
	}
}
