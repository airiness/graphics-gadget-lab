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
		m_DescriptorIncrementSize(m_DX12Device->Get()->GetDescriptorHandleIncrementSize(type))
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
		DX12Descriptor descriptor = {};
		auto countMapIter = m_FreeBlocksByCount.lower_bound(count);


		return descriptor;
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
		return m_AllocatedCount;
	}

	uint32_t DX12DescriptorAllocator::FreeCount() const noexcept
	{
		return m_DescriptorCount - m_AllocatedCount;
	}

	void DX12DescriptorAllocator::AddBlock(OffsetType offset, CountType count) noexcept
	{


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