#pragma once
#include "DX12Resource.h"
namespace graphicsGadgetLab
{


	class DX12Buffer : public DX12Resource
	{
	public:
		explicit DX12Buffer(DX12Device* device,
			D3D12_HEAP_TYPE heapType,
			const CD3DX12_RESOURCE_DESC& resourceDesc,
			D3D12_RESOURCE_STATES initState,
			std::optional<D3D12_CLEAR_VALUE> clearValue = std::nullopt) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12Buffer);
		virtual ~DX12Buffer() noexcept;

		void* Map();
		void UnMap();
	};
}