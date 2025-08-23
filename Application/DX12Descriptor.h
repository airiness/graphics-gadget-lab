#pragma once
namespace gglab
{
	class DX12Device;
	class DX12Descriptor
	{
	public:
		DX12Descriptor() noexcept = default;
		~DX12Descriptor() noexcept = default;

		CD3DX12_CPU_DESCRIPTOR_HANDLE m_CpuHandle = {};
		CD3DX12_GPU_DESCRIPTOR_HANDLE m_GpuHandle = {};
	};

	// TODO: Descriptor Management
	class DX12DescriptorHeap
	{
	public:
		explicit DX12DescriptorHeap(DX12Device* device,
			D3D12_DESCRIPTOR_HEAP_TYPE type,
			D3D12_DESCRIPTOR_HEAP_FLAGS flags,
			uint32_t descriptorCount) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12DescriptorHeap);
		~DX12DescriptorHeap() noexcept;

		ID3D12DescriptorHeap* Get() const noexcept { return m_DescriptorHeap.Get(); }

		DX12Descriptor CreateDescriptor() noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		D3D12_DESCRIPTOR_HEAP_TYPE m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		uint32_t m_DescriptorCount = 0;
		uint32_t m_DescriptorSize = 0;
		ComPtr<ID3D12DescriptorHeap> m_DescriptorHeap;

		uint32_t m_CurrentDescriptorIndex = 0;
	};


}



