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
		desc.NodeMask = 0;

		utility::ThrowIfFailed(m_DX12Device->Get()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_DescriptorHeap)));
	}

	DX12Descriptor DX12DescriptorAllocator::Allocate(uint32_t count) noexcept
	{
		const auto countMapIter = m_FreeBlocksByCount.lower_bound(count);
		if (countMapIter == m_FreeBlocksByCount.end())
		{
			// TODO: Support add new descriptor page
			GGLAB_LOG_WARN("Descriptor Allocator do not have enough continuous count. need:{}", count);
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

	void DX12DescriptorAllocator::Free(DX12Descriptor::Index index, uint32_t count) noexcept
	{

	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE DX12DescriptorAllocator::CpuHandleAt(DX12Descriptor::Index index) const noexcept
	{
		GGLAB_ASSERT_MSG(index >= 0 && index < m_DescriptorCount, "Invalid Cpu Descriptor index.");

		CD3DX12_CPU_DESCRIPTOR_HANDLE cpuStart{ m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart() };
		return cpuStart.Offset(static_cast<INT>(index), static_cast<UINT>(m_DescriptorIncrementSize));
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE DX12DescriptorAllocator::GpuHandleAt(DX12Descriptor::Index index) const noexcept
	{
		GGLAB_ASSERT_MSG(index >= 0 && index < m_DescriptorCount, "Invalid Gpu Descriptor index.");

		CD3DX12_GPU_DESCRIPTOR_HANDLE gpuStart{ m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart() };
		return gpuStart.Offset(static_cast<INT>(index), static_cast<UINT>(m_DescriptorIncrementSize));
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

	void DX12DescriptorAllocator::AddBlock(OffsetType offset, CountType count) noexcept
	{


	}

	DX12Descriptor DX12DescriptorAllocator::AllocateInternal(OffsetType offset, CountType count) noexcept
	{
		DX12Descriptor descriptor = {};
		descriptor.m_Count = count;
		descriptor.m_CpuHandle.InitOffsetted(m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart(), offset, m_DescriptorIncrementSize);
		descriptor.m_GpuHandle.InitOffsetted(m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart(), offset, m_DescriptorIncrementSize);
		descriptor.m_Generation = m_Generation;
		descriptor.m_Type = m_Type;
		return DX12Descriptor();
	}

}


	//DX12Descriptor DX12DescriptorHeap::CreateDescriptor() noexcept
	//{
	//	// TODO: make a cool DesctiptorAllocator
	//	DX12Descriptor descriptor = {};
	//	GGLAB_ASSERT_MSG(m_CurrentDescriptorIndex <= m_DescriptorCount, "DescriptorHeap is Boooooom! you should make a new one!");

	//	descriptor.m_CpuHandle = m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	//	descriptor.m_CpuHandle.Offset(m_CurrentDescriptorIndex * m_DescriptorSize);

	//	if (m_DescriptorHeap->GetDesc().Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
	//	{
	//		descriptor.m_GpuHandle = m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	//		descriptor.m_GpuHandle.Offset(m_CurrentDescriptorIndex * m_DescriptorSize);
	//	}
	//	++m_CurrentDescriptorIndex;
	//	return descriptor;
	//}