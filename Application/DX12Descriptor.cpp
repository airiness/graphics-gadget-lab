#include "Precompiled.h"
#include "DX12Descriptor.h"
#include "DX12DescriptorAllocator.h"

namespace gglab
{
	bool DX12Descriptor::IsValid() const noexcept
	{
		return m_CpuHandle.ptr != 0 && m_Index != InvalidIndex;
	}

	bool DX12Descriptor::IsShaderVisible() const noexcept
	{
		return m_GpuHandle.ptr != 0;
	}

	void DX12Descriptor::Free() noexcept
	{
		if (m_Owner)
		{
			m_Owner->Free(*this);
		}
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE DX12Descriptor::CpuHandleAt(IndexType index) const noexcept
	{
		GGLAB_ASSERT_MSG(index < m_Count, "Invalid Cpu Descriptor index.");
		return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_CpuHandle, static_cast<INT>(index), static_cast<UINT>(m_IncrementSize));
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE DX12Descriptor::GpuHandleAt(IndexType index) const noexcept
	{
		GGLAB_ASSERT_MSG(index < m_Count, "Invalid Gpu Descriptor index.");
		return CD3DX12_GPU_DESCRIPTOR_HANDLE(m_GpuHandle, static_cast<INT>(index), static_cast<UINT>(m_IncrementSize));
	}

	void DX12Descriptor::Reset() noexcept
	{
		m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		m_Index = InvalidIndex;
		m_Count = 0;
		m_IncrementSize = 0;
		m_Generation = 0;
		m_CpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE();
		m_GpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE();
		m_Owner = nullptr;
	}
}