#include "Precompiled.h"
#include "DX12Texture.h"

namespace graphicsGadgetLab
{
	DX12Texture::DX12Texture(DX12Device* device,
		D3D12_HEAP_TYPE heapType,
		const CD3DX12_RESOURCE_DESC& resourceDesc,
		D3D12_RESOURCE_STATES initState,
		std::optional<D3D12_CLEAR_VALUE> clearValue) noexcept :
		DX12Resource(device, heapType, resourceDesc, initState, clearValue)
	{
	}

	DX12Texture::~DX12Texture() noexcept
	{
	}

	void DX12Texture::CreateFromSwapChain(ID3D12Resource* backBuffer) noexcept
	{
		m_Resource.Attach(backBuffer);
		m_ResourceDesc = CD3DX12_RESOURCE_DESC(m_Resource->GetDesc());
		m_ResourceState = D3D12_RESOURCE_STATE_PRESENT;
	}
}

