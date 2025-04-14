#pragma once
namespace D3D12MA
{
	class Allocation;
}

namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12Resource
	{
	public:
		explicit DX12Resource(DX12Device* device, 
			D3D12_HEAP_TYPE heapType,
			const CD3DX12_RESOURCE_DESC& resourceDesc,
			D3D12_RESOURCE_STATES initState,
			std::optional<D3D12_CLEAR_VALUE> clearValue = std::nullopt) noexcept;
		virtual ~DX12Resource() noexcept = default;

		DX12Resource(const DX12Resource&) = delete;
		DX12Resource& operator=(DX12Resource&) = delete;

		DX12Resource(const DX12Resource&&) = default;
		DX12Resource& operator=(DX12Resource&&) = default;

		ID3D12Resource* Get() const;

	private:
		void CreateAllocation(D3D12_HEAP_TYPE heapType);

	private:
		DX12Device* m_DX12Deivce = nullptr;
		CD3DX12_RESOURCE_DESC m_ResourceDesc = {};
		D3D12_RESOURCE_STATES m_ResourceState = D3D12_RESOURCE_STATE_COMMON;
		std::optional<D3D12_CLEAR_VALUE> m_ClearValue = std::nullopt;
		ComPtr<D3D12MA::Allocation> m_D3D12MAAllocation;
	};
}

