#include "Precompiled.h"
#include "DX12RingBuffer.h"
#include "DX12Device.h"
#include "DX12Buffer.h"
#include "MathUtils.h"

namespace gglab
{
	DX12RingBuffer::DX12RingBuffer(const DX12Resource::CreateInfo& createInfo, uint32_t capacityInBytes) noexcept :
		m_Buffer(std::make_unique<DX12Buffer>()),
		m_Allocator(capacityInBytes),
		m_Capacity(capacityInBytes)
	{
		GGLAB_ASSERT_MSG(capacityInBytes > 0, "DX12RingBuffer capacityInBytes must be greater than zero.");

		auto modifiedCreateInfo = createInfo;
		modifiedCreateInfo.m_ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(capacityInBytes));

		m_Buffer->Create(modifiedCreateInfo);

		m_HeapType = createInfo.m_AllocDesc.HeapType;
		if (m_HeapType == D3D12_HEAP_TYPE_UPLOAD || m_HeapType == D3D12_HEAP_TYPE_READBACK)
		{
			m_MappedData = static_cast<std::byte*>(m_Buffer->Map());
			GGLAB_ASSERT_MSG(m_MappedData != nullptr, "DX12RingBuffer failed to map buffer memory.");
			std::memset(m_MappedData, 0, capacityInBytes);
		}

		// Get base GPU address 
		m_GpuVirtualAddress = m_Buffer->GPUVirtualAddress();
	}

	DX12RingBuffer::~DX12RingBuffer()
	{
		if (m_Buffer && m_MappedData)
		{
			m_Buffer->Unmap();
			m_MappedData = nullptr;
		}
	}

	DX12RingBuffer::Span DX12RingBuffer::Allocate(uint32_t sizeInBytes, uint32_t alignment) noexcept
	{
		Span result{};
		if (sizeInBytes == 0)
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12RingBuffer::Allocate called with sizeInBytes = 0.");
			return result;
		}

		const uint32_t alignedSize = utils::AlignUpPow2(sizeInBytes, alignment);
		if (alignedSize > m_Capacity)
		{
			// Requested size exceeds total capacity
			GGLAB_LOG_GRAPHICS_WARN("DX12RingBuffer::Allocate failed: requested size {} exceeds total capacity {}.", alignedSize, m_Capacity);
			return result;
		}

		auto indexSpan = m_Allocator.Allocate(alignedSize);
		if (!indexSpan.IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12RingBuffer::Allocate failed: not enough free space for size {}.", alignedSize);
			return result;
		}

		result.m_IndexSpan = indexSpan;
		result.m_Offset = indexSpan.m_Index;
		result.m_Size = sizeInBytes;

		const auto end = indexSpan.m_Index + indexSpan.m_Count;
		if (end > m_HighWater)
		{
			m_HighWater = end; // Update
		}

		return result;
	}

	DX12RingBuffer::Span DX12RingBuffer::Upload(const void* src, uint32_t sizeInBytes, uint32_t alignment) noexcept
	{
		GGLAB_ASSERT_MSG(src != nullptr, "DX12RingBuffer::Upload called with null source pointer.");
		GGLAB_ASSERT_MSG(sizeInBytes > 0, "DX12RingBuffer::Upload called with sizeInBytes = 0.");
		GGLAB_ASSERT_MSG(IsCPUAccessible(), "DX12RingBuffer::Upload called on non-CPU-accessible buffer.");

		auto span = Allocate(sizeInBytes, alignment);
		if (span.IsValid())
		{
			std::memcpy(GetMappedDataAtOffset(span.m_Offset), src, sizeInBytes);
		}
		return span;
	}

	void DX12RingBuffer::Retire(const Span& span, const DX12FencePoint& fencePoint) noexcept
	{
		if (!span.IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12RingBuffer::Retire called with invalid span.");
			return;
		}

		m_Allocator.RecordRetire(span.m_IndexSpan, fencePoint.GetValue());
	}

	void DX12RingBuffer::ReclaimCompleted(const DX12FencePoint& fencePoint) noexcept
	{
		if (!fencePoint.IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12RingBuffer::ReclaimCompleted called with invalid fence point.");
			return;
		}
		ReclaimCompleted(fencePoint.GetValue());
	}

	void DX12RingBuffer::ReclaimCompleted(uint64_t fenceValue) noexcept
	{
		m_Allocator.FreeCompletedVersion(fenceValue);
	}

	D3D12_GPU_VIRTUAL_ADDRESS DX12RingBuffer::GetGPUVirtualAddressAtOffset(uint32_t offsetInBytes) const noexcept
	{
		return m_GpuVirtualAddress + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(offsetInBytes);
	}

	void* DX12RingBuffer::GetMappedDataAtOffset(uint32_t offsetInBytes) const noexcept
	{
		GGLAB_ASSERT_MSG(IsCPUAccessible(), "DX12RingBuffer::GetMappedDataAtOffset called on non-CPU-accessible buffer.");
		GGLAB_ASSERT_MSG(offsetInBytes < m_Capacity, "DX12RingBuffer::GetMappedDataAtOffset called with offsetInBytes out of bounds.");
		return m_MappedData + offsetInBytes;
	}
}