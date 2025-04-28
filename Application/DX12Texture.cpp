#include "Precompiled.h"
#include "DX12Texture.h"

namespace graphicsGadgetLab
{
	DX12Texture::DX12Texture(DX12Device* device,
		D3D12_HEAP_TYPE heapType,
		const CD3DX12_RESOURCE_DESC& resourceDesc,
		D3D12_RESOURCE_STATES initState,
		std::optional<D3D12_CLEAR_VALUE> clearValue) noexcept :
		m_dx12device(device),

	{
	}

	DX12Texture::DX12Texture(DX12Device* device, ComPtr<ID3D12Resource> resource) noexcept
	{
	}

	DX12Texture::~DX12Texture() noexcept
	{
	}

}

