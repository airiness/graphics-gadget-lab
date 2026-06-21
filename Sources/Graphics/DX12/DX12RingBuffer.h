#pragma once
#include "Core/Allocator/RingSpanAllocator.h"
#include "Graphics/DX12/DX12FencePoint.h"
#include "Graphics/GPUStructures.h"
#include "Graphics/DX12/DX12Buffer.h"

namespace gglab
{
	class DX12Device;
	class DX12Buffer;
	class DX12RingBuffer
	{
	public:
		struct Span
		{
			uint32_t m_Offset = ~0u;
			uint32_t m_Size = 0;
			RingSpanAllocator::IndexSpan m_IndexSpan{};
			bool IsValid() const noexcept { return m_IndexSpan.IsValid(); }
		};

	public:
		DX12RingBuffer() noexcept = default;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12RingBuffer);
		~DX12RingBuffer();

		void Create(const DX12Resource::CreateInfo& createInfo, uint32_t capacityInBytes) noexcept;

		Span Allocate(uint32_t sizeInBytes, uint32_t alignment = GGLAB_GPU_STRUCTURE_ALIGNAS_VALUE) noexcept;
		Span Upload(const void* src, uint32_t sizeInBytes, uint32_t alignment = GGLAB_GPU_STRUCTURE_ALIGNAS_VALUE) noexcept;
		void Retire(const Span& span, const DX12FencePoint& fencePoint) noexcept;
		void ReclaimCompleted(const DX12FencePoint& fencePoint) noexcept;
		void ReclaimCompleted(uint64_t completedFence) noexcept;

		bool IsValid() const noexcept { return m_Buffer != nullptr && m_Allocator != nullptr; }
		DX12Buffer* GetBuffer() const noexcept { return m_Buffer.get(); }
		ID3D12Resource* GetResource() const noexcept { return m_Buffer ? m_Buffer->Get() : nullptr; }
		uint32_t GetCapacity() const noexcept { return m_Capacity; }
		uint32_t GetCurrentUsage() const noexcept { return m_Allocator ? m_Allocator->GetCurrentUsage() : 0; }
		uint32_t GetHighWater() const noexcept { return m_Allocator ? m_Allocator->GetHighWater() : 0; }
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const noexcept { return m_GpuVirtualAddress; }
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddressAtOffset(uint32_t offsetInBytes) const noexcept;
		bool IsCPUAccessible() const noexcept { return m_HeapType == D3D12_HEAP_TYPE_UPLOAD || m_HeapType == D3D12_HEAP_TYPE_READBACK; }
		void* GetMappedDataAtOffset(uint32_t offsetInBytes) const noexcept;

	private:
		std::unique_ptr<DX12Buffer> m_Buffer;
		std::unique_ptr<RingSpanAllocator> m_Allocator;
		D3D12_GPU_VIRTUAL_ADDRESS m_GpuVirtualAddress = 0;
		uint32_t m_Capacity = 0;
		D3D12_HEAP_TYPE m_HeapType = D3D12_HEAP_TYPE_DEFAULT;
		std::byte* m_MappedData = nullptr;
	};
}
