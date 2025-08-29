#include "Precompiled.h"
#include "DX12Descriptor.h"

#include "DX12DescriptorAllocatorBase.h"
#include "DX12Device.h"
#include "Utility.h"

namespace gglab
{
	bool DX12Descriptor::IsValid() const noexcept
	{
		return m_CpuHandle.ptr != 0 && m_Index != InvalidIndex && m_Count > 0;
	}

	bool DX12Descriptor::IsShaderVisible() const noexcept
	{
		return m_GpuHandle.ptr != 0;
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE DX12Descriptor::CpuHandleAt(IndexType index) const noexcept
	{
		GGLAB_ASSERT_MSG(index < m_Count, "Invalid Cpu Descriptor index.");
		return { m_CpuHandle, static_cast<INT>(index), static_cast<UINT>(m_IncrementSize) };
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE DX12Descriptor::GpuHandleAt(IndexType index) const noexcept
	{
		GGLAB_ASSERT_MSG(index < m_Count, "Invalid Gpu Descriptor index.");
		return { m_GpuHandle, static_cast<INT>(index), static_cast<UINT>(m_IncrementSize) };
	}

	void DX12Descriptor::Free() noexcept
	{
		if (!m_Owner)
		{
			GGLAB_ASSERT_MSG(false, "Free(): Descriptor owner is null.");
		}
		m_Owner->FreeDescriptorInternal(*this);
	}

	void DX12Descriptor::Retire(const DX12FencePoint& fencePoint) const noexcept
	{
		if (!m_Owner)
		{
			GGLAB_ASSERT_MSG(false, "Retire(): Descriptor owner is null.");
		}
		m_Owner->RetireDescriptorInternal(*this, fencePoint);
	}

	void DX12Descriptor::Reset() noexcept
	{
		m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		m_Index = InvalidIndex;
		m_Count = 0;
		m_IncrementSize = 0;
		m_CpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE();
		m_GpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE();
		m_Owner = nullptr;
	}

	DX12DescriptorView DX12Descriptor::ToView(IndexType index) const noexcept
	{
		DX12DescriptorView descriptorView;
		descriptorView.m_CpuHandle = CpuHandleAt(index);
		descriptorView.m_GpuHandle = GpuHandleAt(index);

		return descriptorView;
	}

	DX12DescriptorHeap::DX12DescriptorHeap(DX12Device* dx12Device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_DESCRIPTOR_HEAP_FLAGS flags,
		uint32_t descriptorCount) noexcept :
		m_DX12Device(dx12Device),
		m_Type(type),
		m_Flags(flags),
		m_DescriptorCount(descriptorCount),
		m_IncrementSize(dx12Device->Get()->GetDescriptorHandleIncrementSize(type))
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = m_Type;
		desc.NumDescriptors = m_DescriptorCount;
		desc.Flags = m_Flags;

		utility::ThrowIfFailed(m_DX12Device->Get()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_D3D12DescriptorHeap)));
	}

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