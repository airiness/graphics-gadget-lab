#pragma once
namespace gglab
{
	class DX12Device;
	class DX12Descriptor
	{
	public:
		using Index = uint32_t;

	public:
		DX12Descriptor() noexcept = default;
		~DX12Descriptor() = default;

		bool IsValid() const noexcept;
		bool IsShaderVisible() const noexcept;
		D3D12_DESCRIPTOR_HEAP_TYPE Type() const noexcept;
		CD3DX12_CPU_DESCRIPTOR_HANDLE CpuHandleAt(Index index, uint32_t stride) const noexcept;
		CD3DX12_GPU_DESCRIPTOR_HANDLE GpuHandleAt(Index index, uint32_t stride) const noexcept;

	private:
		static constexpr Index InvalidIndex = std::numeric_limits<Index>::max();

	private:
		D3D12_DESCRIPTOR_HEAP_TYPE m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		Index m_Index = InvalidIndex;
		uint32_t m_Count = 0;
		uint32_t m_Generation = 0;
		CD3DX12_CPU_DESCRIPTOR_HANDLE m_CpuHandle = {};
		CD3DX12_GPU_DESCRIPTOR_HANDLE m_GpuHandle = {};

	private:
		friend class DX12DescriptorAllocator;
		friend class DX12DescriptorRingAllocator;
	};
}