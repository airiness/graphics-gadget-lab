#include "Core/Precompiled.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/DX12/DX12Device.h"
#include "Core/HResult.h"

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

	CD3DX12_CPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::CpuHandleStart() const noexcept
	{
		return CpuHandleAt(0);
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::GpuHandleStart() const noexcept
	{
		return GpuHandleAt(0);
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::CpuHandleAt(uint32_t index) const noexcept
	{
		GGLAB_ASSERT_MSG(index < m_DescriptorCount, "Invalid Descriptor index.");
		return { m_D3D12DescriptorHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(index), static_cast<UINT>(m_IncrementSize) };
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::GpuHandleAt(uint32_t index) const noexcept
	{
		GGLAB_ASSERT_MSG(index < m_DescriptorCount, "Invalid Descriptor index.");
		if (m_Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
		{
			return { m_D3D12DescriptorHeap->GetGPUDescriptorHandleForHeapStart(), static_cast<INT>(index), static_cast<UINT>(m_IncrementSize) };
		}

		return CD3DX12_GPU_DESCRIPTOR_HANDLE();
	}
}