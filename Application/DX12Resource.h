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
		explicit DX12Resource(DX12Device* device, D3D12_RESOURCE_DESC& resourceDesc) noexcept;
		virtual ~DX12Resource() noexcept;

		DX12Resource(const DX12Resource&) = delete;
		DX12Resource& operator=(DX12Resource&) = delete;

		DX12Resource(const DX12Resource&&) = default;
		DX12Resource& operator=(DX12Resource&&) = default;

		ID3D12Resource* Get() const;

	private:
		void CreateAllocation();

	private:
		DX12Device* m_DX12Deivce = nullptr;
		ComPtr<D3D12MA::Allocation> m_D3D12MAAllocation;
	};
}

