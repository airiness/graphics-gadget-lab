#include "Precompiled.h"
#include "DX12DescriptorAllocator.h"
#include "DX12Device.h"
#include "Utility.h"

namespace gglab
{
	DX12DescriptorAllocator::DX12DescriptorAllocator(DX12Device* dx12Device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_DESCRIPTOR_HEAP_FLAGS flags,
		uint32_t descriptorCount) noexcept :
		m_DX12Device(dx12Device),
		m_Type(type),
		m_Flags(flags),
		m_DescriptorCount(descriptorCount),
		m_DescriptorIncrementSize(m_DX12Device->Get()->GetDescriptorHandleIncrementSize(type)),
		m_FreeCount(descriptorCount)
	{
		AddBlock(0, descriptorCount);

		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = m_Type;
		desc.NumDescriptors = m_DescriptorCount;
		desc.Flags = m_Flags;

		utility::ThrowIfFailed(m_DX12Device->Get()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_DescriptorHeap)));
	}

	DX12Descriptor DX12DescriptorAllocator::Allocate(uint32_t count) noexcept
	{
		std::lock_guard lock(m_Mutex);

		const auto countMapIter = m_FreeBlocksByCount.lower_bound(count);
		if (countMapIter == m_FreeBlocksByCount.end())
		{
			// TODO: Add new descriptor page
			GGLAB_LOG_WARN("Descriptor Allocator do not have enough continuous count. requestCount:{}, capacity:{}, freeCount:{}, type:{}, flags:{}",
				count, m_DescriptorCount, m_FreeCount, m_Type, m_Flags);
			return DX12Descriptor();
		}

		const auto& offsetMapIter = countMapIter->second;
		const auto allocOffset = offsetMapIter->first;
		const auto newOffset = allocOffset + count;
		const auto newCount = countMapIter->first - count;

		// Erase offset iterator first.
		m_FreeBlocksByOffset.erase(offsetMapIter);
		m_FreeBlocksByCount.erase(countMapIter);

		if (newCount > 0)
		{
			AddBlock(newOffset, newCount);
		}
		m_FreeCount -= count;

		return AllocateInternal(allocOffset, count);
	}

	void DX12DescriptorAllocator::Free(DX12Descriptor& descriptor) noexcept
	{
		std::lock_guard lock(m_Mutex);

		GGLAB_ASSERT_MSG(descriptor.m_Owner == this, "This Descriptor do not belong this Allocator.");

		const auto offset = descriptor.m_Index;
		const auto count = descriptor.m_Count;

		const auto nextOffsetIter = m_FreeBlocksByOffset.upper_bound(offset);
		auto prevOffsetIter = nextOffsetIter;
		if (nextOffsetIter != m_FreeBlocksByOffset.begin())
		{
			--prevOffsetIter;
		}
		else
		{
			prevOffsetIter = m_FreeBlocksByOffset.end();
		}

		auto newOffset = offset;
		auto newCount = count;

		// has prev free block
		if (prevOffsetIter != m_FreeBlocksByOffset.end() && prevOffsetIter->first + prevOffsetIter->second.m_Count == offset)
		{
			newOffset = prevOffsetIter->first;
			newCount += prevOffsetIter->second.m_Count;

			m_FreeBlocksByCount.erase(prevOffsetIter->second.m_OrderBySizeIter);
			m_FreeBlocksByOffset.erase(prevOffsetIter);
		}

		// has next free block
		if (nextOffsetIter != m_FreeBlocksByOffset.end() && offset + count == nextOffsetIter->first)
		{
			newCount += nextOffsetIter->second.m_Count;
			m_FreeBlocksByCount.erase(nextOffsetIter->second.m_OrderBySizeIter);
			m_FreeBlocksByOffset.erase(nextOffsetIter);
		}

		AddBlock(newOffset, newCount);
		m_FreeCount += count;

		// TODO: per slot generation.
		++m_Generation;

		descriptor.Reset();
	}

	uint32_t DX12DescriptorAllocator::Capacity() const noexcept
	{
		return m_DescriptorCount;
	}

	uint32_t DX12DescriptorAllocator::DescriptorIncrementSize() const noexcept
	{
		return m_DescriptorIncrementSize;
	}

	uint32_t DX12DescriptorAllocator::AllocatedCount() const noexcept
	{
		return m_DescriptorCount - m_FreeCount;
	}

	uint32_t DX12DescriptorAllocator::FreeCount() const noexcept
	{
		return m_FreeCount;
	}

	ID3D12DescriptorHeap* DX12DescriptorAllocator::Heap() const noexcept
	{
		return m_DescriptorHeap.Get();
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE DX12DescriptorAllocator::CpuStart() const noexcept
	{
		return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE DX12DescriptorAllocator::GpuStart() const noexcept
	{
		return (m_Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) ?
			CD3DX12_GPU_DESCRIPTOR_HANDLE(m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart()) :
			CD3DX12_GPU_DESCRIPTOR_HANDLE();
	}

	void DX12DescriptorAllocator::AddBlock(OffsetType offset, CountType count) noexcept
	{
		auto offsetMapIter = m_FreeBlocksByOffset.emplace(offset, FreeBlock{ count });
		if (offsetMapIter.second)
		{
			const auto countMapIter = m_FreeBlocksByCount.emplace(count, offsetMapIter.first);
			offsetMapIter.first->second.m_OrderBySizeIter = countMapIter;
		}
	}

	DX12Descriptor DX12DescriptorAllocator::AllocateInternal(OffsetType offset, CountType count) noexcept
	{
		DX12Descriptor descriptor = {};
		descriptor.m_Type = m_Type;
		descriptor.m_Index = offset;
		descriptor.m_Count = count;
		descriptor.m_IncrementSize = m_DescriptorIncrementSize;
		descriptor.m_Generation = m_Generation;
		descriptor.m_CpuHandle.InitOffsetted(m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart(), offset, m_DescriptorIncrementSize);
		if (m_Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
		{
			descriptor.m_GpuHandle.InitOffsetted(m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart(), offset, m_DescriptorIncrementSize);
		}
		else
		{
			descriptor.m_GpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE();
		}
		descriptor.m_Owner = this;
		return descriptor;
	}
}