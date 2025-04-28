#pragma once

namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12Texture
	{
	public:
		explicit DX12Texture(DX12Device* device,
			D3D12_HEAP_TYPE heapType,
			const CD3DX12_RESOURCE_DESC& resourceDesc,
			D3D12_RESOURCE_STATES initState,
			std::optional<D3D12_CLEAR_VALUE> clearValue = std::nullopt) noexcept;
		explicit DX12Texture(DX12Device* device, ComPtr<ID3D12Resource> resource) noexcept;
		virtual ~DX12Texture() noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;

		D3D12MA::Allocation* m_Allocation = nullptr;
		D3D12_RESOURCE_STATES m_ResourceState = D3D12_RESOURCE_STATE_COMMON;
		std::optional<D3D12_CLEAR_VALUE> m_ClearValue;


	};
}