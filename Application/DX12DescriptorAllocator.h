#pragma once
#include "DX12Descriptor.h"

namespace gglab
{
	class DX12Device;
	class DX12DescriptorAllocator
	{
	private:
		using OffsetType = uint32_t;
		using CountType = uint32_t;
		struct FreeBlock;
		using FreeBlocksByOffset = std::map<OffsetType, FreeBlock>;
		using FreeBlocksByCount = std::multimap<CountType, FreeBlocksByOffset::iterator>;
		struct FreeBlock
		{
			CountType m_Count = 0;
			FreeBlocksByCount::iterator m_OrderBySizeIter;
		};

	public:
		explicit DX12DescriptorAllocator(DX12Device* dx12Device,
			D3D12_DESCRIPTOR_HEAP_TYPE type,
			D3D12_DESCRIPTOR_HEAP_FLAGS flags,
			uint32_t descriptorCount) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12DescriptorAllocator);
		~DX12DescriptorAllocator() = default;

		DX12Descriptor Allocate(uint32_t count = 1) noexcept;
		void Free(DX12Descriptor::Index index, uint32_t count = 1) noexcept;

		CD3DX12_CPU_DESCRIPTOR_HANDLE CpuHandleAt(DX12Descriptor::Index index) const noexcept;
		CD3DX12_GPU_DESCRIPTOR_HANDLE GpuHandleAt(DX12Descriptor::Index index) const noexcept;

		uint32_t Capacity() const noexcept;
		uint32_t DescriptorIncrementSize() const noexcept;
		uint32_t AllocatedCount() const noexcept;
		uint32_t FreeCount() const noexcept;

	private:
		void AddBlock(OffsetType offset, CountType count) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		D3D12_DESCRIPTOR_HEAP_TYPE m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		D3D12_DESCRIPTOR_HEAP_FLAGS m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		uint32_t m_DescriptorCount = 0;
		uint32_t m_DescriptorIncrementSize = 0;
		ComPtr<ID3D12DescriptorHeap> m_DescriptorHeap;
		
		FreeBlocksByOffset m_FreeBlocksByOffset;
		FreeBlocksByCount m_FreeBlocksByCount;

		uint32_t m_AllocatedCount = 0;
		uint32_t m_FreeSize = 0;
		uint32_t m_Generation = 0;

		std::mutex m_Mutex;
	};
}