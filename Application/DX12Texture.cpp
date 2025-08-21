#include "Precompiled.h"
#include "DX12Texture.h"

namespace graphicsGadgetLab
{
	void DX12Texture::Create2D(D3D12MA::Allocator& allocator,
		DXGI_FORMAT format,
		uint32_t width,
		uint32_t height,
		uint16_t arraySize,
		uint16_t miplevels,
		uint32_t sampleCount,
		D3D12_RESOURCE_FLAGS flags,
		D3D12_RESOURCE_STATES initStates,
		const std::optional<D3D12_CLEAR_VALUE>& clearValue,
		D3D12_HEAP_TYPE heapType,
		D3D12MA::ALLOCATION_FLAGS allocFlags) noexcept
	{
	}

	void DX12Texture::AdoptFromSwapChain(ComPtr<ID3D12Resource> backBuffer) noexcept
	{
		AdoptExternal(std::move(backBuffer), D3D12_RESOURCE_STATE_PRESENT);
	}
}