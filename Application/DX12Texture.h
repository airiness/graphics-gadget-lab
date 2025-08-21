#pragma once
#include "DX12Resource.h"

namespace graphicsGadgetLab
{
	class DX12Texture : public DX12Resource
	{
	public:
		DX12Texture() noexcept = default;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12Texture);
		virtual ~DX12Texture() = default;

		void Create2D(D3D12MA::Allocator& allocator,
			DXGI_FORMAT format,
			uint32_t width,
			uint32_t height,
			uint16_t arraySize = 1,
			uint16_t miplevels = 1,
			uint32_t sampleCount = 1,
			D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATES initStates = D3D12_RESOURCE_STATE_COMMON,
			const std::optional<D3D12_CLEAR_VALUE>& clearValue = std::nullopt,
			D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT,
			D3D12MA::ALLOCATION_FLAGS allocFlags = D3D12MA::ALLOCATION_FLAGS::ALLOCATION_FLAG_NONE) noexcept;

		void AdoptFromSwapChain(ComPtr<ID3D12Resource> backBuffer) noexcept;
	};
}