#pragma once
#include "DX12Resource.h"

namespace graphicsGadgetLab
{
	class DX12Texture : public DX12Resource
	{
	public:
		DX12Texture() = default;
		explicit DX12Texture(DX12Device* device,
			D3D12_HEAP_TYPE heapType,
			const CD3DX12_RESOURCE_DESC& resourceDesc,
			D3D12_RESOURCE_STATES initState,
			std::optional<D3D12_CLEAR_VALUE> clearValue = std::nullopt) noexcept;
		virtual ~DX12Texture() noexcept;

		void CreateFromSwapChain() noexcept;

	private:

	};
}