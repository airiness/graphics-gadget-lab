#include "Precompiled.h"
#include "DX12DescriptorAllocator.h"
#include "DX12Device.h"

namespace gglab
{
	DX12DescriptorAllocator::DX12DescriptorAllocator(DX12Device* dx12Device, 
		D3D12_DESCRIPTOR_HEAP_TYPE type, 
		D3D12_DESCRIPTOR_HEAP_FLAGS flags, 
		uint32_t descriptorCount) noexcept
	{
	}
}



//DX12DescriptorHeap::DX12DescriptorHeap(DX12Device* device,
	//	D3D12_DESCRIPTOR_HEAP_TYPE type,
	//	D3D12_DESCRIPTOR_HEAP_FLAGS flags,
	//	uint32_t descriptorCount) noexcept :
	//	m_DX12Device(device),
	//	m_Type(type),
	//	m_DescriptorCount(descriptorCount)
	//{
	//	m_DescriptorSize = m_DX12Device->Get()->GetDescriptorHandleIncrementSize(m_Type);

	//	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	//	desc.NumDescriptors = m_DescriptorCount;
	//	desc.Type = m_Type;
	//	desc.Flags = flags;
	//	desc.NodeMask = 0;
	//	utility::ThrowIfFailed(m_DX12Device->Get()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_DescriptorHeap)));
	//}

	//DX12DescriptorHeap::~DX12DescriptorHeap()
	//{
	//}

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