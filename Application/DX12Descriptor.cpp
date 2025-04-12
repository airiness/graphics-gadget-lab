#include "Precompiled.h"
#include "DX12Descriptor.h"
#include "DX12Device.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	DX12DescriptorHeap::DX12DescriptorHeap(DX12Device* device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_DESCRIPTOR_HEAP_FLAGS flags,
		uint32_t descriptorCount) noexcept :
		m_DX12Device(device),
		m_Type(type),
		m_DescriptorCount(descriptorCount)
	{
		m_DescriptorSize = m_DX12Device->Get()->GetDescriptorHandleIncrementSize(m_Type);

		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = m_DescriptorCount;
		desc.Type = m_Type;
		desc.Flags = flags;
		desc.NodeMask = 0;
		utility::ThrowIfFailed(m_DX12Device->Get()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_DescriptorHeap)));
	}

	DX12DescriptorHeap::~DX12DescriptorHeap() noexcept
	{
	}
}