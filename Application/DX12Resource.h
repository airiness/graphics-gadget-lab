#pragma once

namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12Resource
	{
	public:
		DX12Resource() = default;
		explicit DX12Resource(DX12Device* device,
			D3D12_HEAP_TYPE heapType,
			const CD3DX12_RESOURCE_DESC& resourceDesc,
			D3D12_RESOURCE_STATES initState,
			std::optional<D3D12_CLEAR_VALUE> clearValue = std::nullopt) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12Resource);
		virtual ~DX12Resource() noexcept = default;

		ID3D12Resource* Get() const;

		void SetDebugName(const wchar_t* name) noexcept;

	protected:
		void CreateAllocation(D3D12_HEAP_TYPE heapType);

	protected:
		DX12Device* m_DX12Deivce = nullptr;
		CD3DX12_RESOURCE_DESC m_ResourceDesc = {};
		D3D12_RESOURCE_STATES m_ResourceState = D3D12_RESOURCE_STATE_COMMON;
		std::optional<D3D12_CLEAR_VALUE> m_ClearValue = std::nullopt;
		ComPtr<D3D12MA::Allocation> m_Allocation;
		ComPtr<ID3D12Resource> m_Resource;
	};
}

