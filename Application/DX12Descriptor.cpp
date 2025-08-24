#include "Precompiled.h"
#include "DX12Descriptor.h"
#include "DX12Device.h"
#include "Utility.h"

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

	D3D12_DESCRIPTOR_HEAP_TYPE DX12Descriptor::Type() const noexcept
	{
		return m_Type;
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE DX12Descriptor::CpuHandleAt(Index index, uint32_t stride) const noexcept
	{
		return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_CpuHandle, static_cast<INT>(index) * stride);
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE DX12Descriptor::GpuHandleAt(Index index, uint32_t stride) const noexcept
	{
		return CD3DX12_GPU_DESCRIPTOR_HANDLE(m_GpuHandle, static_cast<INT>(index) * stride);
	}
}