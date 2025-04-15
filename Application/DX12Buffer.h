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
		virtual ~DX12Buffer() noexcept;

		void* Map();
		void UnMap();
	};

	// TODO: A template support constant buffer class
	template<typename T>
	class DX12ConstantBuffer
	{
	public:
		
	private:
		std::unique_ptr<DX12Buffer> m_ConstantBuffer;
		T* m_MappedData = nullptr;
	};
}