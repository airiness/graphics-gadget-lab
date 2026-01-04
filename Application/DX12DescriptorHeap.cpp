#include "Precompiled.h"
#include "DX12DescriptorHeap.h"
#include "DX12Device.h"
#include "HResult.h"

namespace gglab
{
	DX12DescriptorHeap::DX12DescriptorHeap(const CreateInfo& createInfo) noexcept :
		m_DX12Device(createInfo.m_DX12Device),
		m_Type(createInfo.m_Type),
		m_Flags(createInfo.m_Flags),
		m_DescriptorCount(createInfo.m_DescriptorCount),
		m_IncrementSize(m_DX12Device->Get()->GetDescriptorHandleIncrementSize(m_Type))
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = m_Type;
		desc.NumDescriptors = m_DescriptorCount;
		desc.Flags = m_Flags;

		GGLAB_HR_DX(m_DX12Device->Get()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_D3D12DescriptorHeap)),
			m_DX12Device->Get());
	}

	DX12DescriptorHeap::~DX12DescriptorHeap() = default;

	CD3DX12_CPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::CpuStart() const noexcept
	{
		return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_D3D12DescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::GpuStart() const noexcept
	{
		return (m_Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) ?
			CD3DX12_GPU_DESCRIPTOR_HANDLE(m_D3D12DescriptorHeap->GetGPUDescriptorHandleForHeapStart()) :
			CD3DX12_GPU_DESCRIPTOR_HANDLE();
	}
}