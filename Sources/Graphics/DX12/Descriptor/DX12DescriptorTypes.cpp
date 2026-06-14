#include "Core/Precompiled.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorTypes.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorAllocatorBase.h"

namespace gglab
{
	DX12DescriptorHandle::DX12DescriptorHandle(DX12DescriptorHandle&& rhs) noexcept
	{
		MoveFrom(rhs);
		rhs.Reset();
	}

	DX12DescriptorHandle& DX12DescriptorHandle::operator=(DX12DescriptorHandle&& rhs) noexcept
	{
		if (this != &rhs)
		{
#if defined (BUILD_DEBUG)
			GGLAB_ASSERT_MSG(!IsValid(), "DX12DescriptorHandle overwritten while still valid (leak). Call Free/Retire first.");
#endif
			Reset();
			MoveFrom(rhs);
			rhs.Reset();
		}
		return *this;
	}

	DX12DescriptorHandle::~DX12DescriptorHandle()
	{
#if defined (BUILD_DEBUG)
		if (IsValid())
		{
			GGLAB_ASSERT_MSG(false, "DX12DescriptorHandle leaked: must Free()/Retire() before destruction.");
		}
#endif
	}

	bool DX12DescriptorHandle::IsValid() const noexcept
	{
		return
			m_Index != InvalidIndex &&
			m_Count > 0 &&
			m_OwnerAllocator != nullptr &&
			m_OwnerAllocator->GetHeap() != nullptr;
	}

	bool DX12DescriptorHandle::IsShaderVisible() const noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "IsShaderVisible(), Invalid DX12DescriptorHandle.");
		return m_OwnerAllocator->IsShaderVisible();
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE DX12DescriptorHandle::CpuHandleAt(uint32_t offset) const noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "CpuHandleAt(): Invalid DX12DescriptorHandle.");
		GGLAB_ASSERT_MSG(offset < m_Count, "CpuHandleAt(): offset out of range.");
		return m_OwnerAllocator->CpuHandleAtGlobalIndex(m_Index + offset);
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE DX12DescriptorHandle::GpuHandleAt(uint32_t offset) const noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "GpuHandleAt(): Invalid DX12DescriptorHandle.");
		GGLAB_ASSERT_MSG(offset < m_Count, "GpuHandleAt(): offset out of range.");
		return m_OwnerAllocator->GpuHandleAtGlobalIndex(m_Index + offset);
	}

	D3D12_DESCRIPTOR_HEAP_TYPE DX12DescriptorHandle::HeapType() const noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "Type(), Invalid DX12DescriptorHandle.");
		return m_OwnerAllocator->HeapType();
	}

	DX12DescriptorView DX12DescriptorHandle::ToDescriptorView(uint32_t offset) const noexcept
	{
		return {
			.m_CpuHandle = CpuHandleAt(offset),
			.m_GpuHandle = IsShaderVisible() ? GpuHandleAt(offset) : CD3DX12_GPU_DESCRIPTOR_HANDLE{},
			.m_Index = m_Index + offset,
#if defined(BUILD_DEBUG)
			.m_DebugType = HeapType()
#endif
		};
	}

	DX12DescriptorID DX12DescriptorHandle::ToDescriptorId() const noexcept
	{
		return { .m_Index = m_Index, .m_Generation = m_Generation };
	}

	void DX12DescriptorHandle::Free() noexcept
	{
		if (!IsValid())
		{
			return;
		}
		m_OwnerAllocator->FreeHandleInternal(*this);
		Reset();
	}

	void DX12DescriptorHandle::Retire(const DX12FencePoint& fencePoint) noexcept
	{
		if (!IsValid())
		{
			return;
		}

		m_OwnerAllocator->RetireHandleInternal(*this, fencePoint);
		Reset();
	}

	void DX12DescriptorHandle::Reset() noexcept
	{
		m_Index = InvalidIndex;
		m_Count = 0;
		m_Generation = 0;
		m_OwnerAllocator = nullptr;
	}

	void DX12DescriptorHandle::MoveFrom(DX12DescriptorHandle& rhs) noexcept
	{
		m_Index = rhs.m_Index;
		m_Count = rhs.m_Count;
		m_Generation = rhs.m_Generation;
		m_OwnerAllocator = rhs.m_OwnerAllocator;
		rhs.Reset();
	}
}
