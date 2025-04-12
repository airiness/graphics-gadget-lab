#pragma once
namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12DescriptorHeap
	{
	public:
		explicit DX12DescriptorHeap(DX12Device* device,
			D3D12_DESCRIPTOR_HEAP_TYPE type,
			D3D12_DESCRIPTOR_HEAP_FLAGS flags,
			uint32_t descriptorCount) noexcept;

		DX12DescriptorHeap(const DX12DescriptorHeap&) = delete;
		DX12DescriptorHeap& operator=(const DX12DescriptorHeap&) = delete;
		DX12DescriptorHeap(DX12DescriptorHeap&&) = delete;
		DX12DescriptorHeap& operator=(DX12DescriptorHeap&&) = delete;
		~DX12DescriptorHeap() noexcept;

		ID3D12DescriptorHeap* Get() const noexcept { return m_DescriptorHeap.Get(); }

	private:
		DX12Device* m_DX12Device = nullptr;
		D3D12_DESCRIPTOR_HEAP_TYPE m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		uint32_t m_DescriptorCount = 0;
		uint32_t m_DescriptorSize = 0;
		ComPtr<ID3D12DescriptorHeap> m_DescriptorHeap;
	};

}



