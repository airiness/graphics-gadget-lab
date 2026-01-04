#pragma once

namespace gglab
{
	class DX12Device;
	class DX12DescriptorHeap
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_DX12Device = nullptr;
			D3D12_DESCRIPTOR_HEAP_TYPE m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			D3D12_DESCRIPTOR_HEAP_FLAGS m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			uint32_t m_DescriptorCount = 0;
		};

	public:
		explicit DX12DescriptorHeap(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12DescriptorHeap);
		~DX12DescriptorHeap();

		uint32_t DescriptorCount() const noexcept { return m_DescriptorCount; }
		uint32_t DescriptorIncrementSize() const noexcept { return m_IncrementSize; }

		ID3D12DescriptorHeap* Get() const noexcept { return m_D3D12DescriptorHeap.Get(); }
		D3D12_DESCRIPTOR_HEAP_TYPE Type() const noexcept { return m_Type; }
		D3D12_DESCRIPTOR_HEAP_FLAGS Flags() const noexcept { return m_Flags; }

		CD3DX12_CPU_DESCRIPTOR_HANDLE CpuStart() const noexcept;
		CD3DX12_GPU_DESCRIPTOR_HANDLE GpuStart() const noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		D3D12_DESCRIPTOR_HEAP_TYPE m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		D3D12_DESCRIPTOR_HEAP_FLAGS m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		uint32_t m_DescriptorCount = 0;
		uint32_t m_IncrementSize = 0;
		ComPtr<ID3D12DescriptorHeap> m_D3D12DescriptorHeap;
	};

}