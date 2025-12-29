#include "Precompiled.h"
#include "DX12Descriptor.h"

#include "DX12DescriptorAllocatorBase.h"
#include "DX12Device.h"
#include "HResult.h"

namespace gglab
{
	DX12Descriptor::DX12Descriptor(DX12Descriptor&& rhs) noexcept
	{
		MoveFrom(rhs);
	}

	DX12Descriptor& DX12Descriptor::operator=(DX12Descriptor&& rhs) noexcept
	{
		if (this != &rhs)
		{
#if defined (BUILD_DEBUG)
			GGLAB_ASSERT_MSG(!IsValid(), "DX12Descriptor overwritten while still valid (leak). Call Free/Retire first.");
#endif
			Reset();
			MoveFrom(rhs);
		}
		return *this;
	}

	DX12Descriptor::~DX12Descriptor()
	{
#if defined (BUILD_DEBUG)
		if (IsValid())
		{
			GGLAB_ASSERT_MSG(false, "DX12Descriptor leaked: must Free()/Retire() before destruction.");
		}
#endif
	}

	bool DX12Descriptor::IsValid() const noexcept
	{
		return 
			m_CpuHandle.ptr != 0 &&
			m_Index != InvalidIndex &&
			m_Count > 0 && 
			m_Owner != nullptr;
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
		GGLAB_ASSERT_MSG(m_Owner, "Free(): Descriptor owner is null.");
		if (!IsValid())
		{
			return;
		}
		
		m_Owner->FreeDescriptorInternal(*this);
		Reset();
	}

	void DX12Descriptor::Retire(const DX12FencePoint& fencePoint) noexcept
	{
		GGLAB_ASSERT_MSG(m_Owner, "Retire(): Descriptor owner is null.");
		if (!IsValid())
		{
			return;
		}
		
		m_Owner->RetireDescriptorInternal(*this, fencePoint);
		Reset();
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

	void DX12Descriptor::MoveFrom(DX12Descriptor& rhs) noexcept
	{
		m_Type = rhs.m_Type;
		m_Index = rhs.m_Index;
		m_Count = rhs.m_Count;
		m_IncrementSize = rhs.m_IncrementSize;
		m_CpuHandle = rhs.m_CpuHandle;
		m_GpuHandle = rhs.m_GpuHandle;
		m_Owner = rhs.m_Owner;
		rhs.Reset();
	}

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