#include "Precompiled.h"
#include "DX12DescriptorTypes.h"
#include "DX12DescriptorHeap.h"
#include "DX12DescriptorAllocatorBase.h"

namespace gglab
{
	DX12DescriptorHandle::DX12DescriptorHandle(DX12DescriptorHandle&& rhs) noexcept
	{
		MoveFrom(rhs);
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
			m_Owner != nullptr &&
			m_Owner->GetHeap() != nullptr;
	}

	bool DX12DescriptorHandle::IsShaderVisible() const noexcept
	{
		auto* heap = m_Owner->GetHeap();
		GGLAB_ASSERT(heap);

		return (heap->Flags() & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
	}

	DX12DescriptorView DX12DescriptorHandle::ToView(uint32_t localIndex) const noexcept
	{
		DX12DescriptorView descriptorView
		{
			.m_CpuHandle = CpuHandle(localIndex),
			.m_GpuHandle = GpuHandle(localIndex)
		};
		return descriptorView;
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE DX12DescriptorHandle::CpuHandle(uint32_t localIndex) const noexcept
	{
		auto* heap = m_Owner->GetHeap();
		GGLAB_ASSERT(heap);

		return heap->CpuHandleAt(m_Index + localIndex);
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE DX12DescriptorHandle::GpuHandle(uint32_t localIndex) const noexcept
	{
		auto* heap = m_Owner->GetHeap();
		GGLAB_ASSERT(heap);

		return heap->GpuHandleAt(m_Index + localIndex);
	}

	void DX12DescriptorHandle::Free() noexcept
	{
		GGLAB_ASSERT_MSG(m_Owner, "Free(): Descriptor owner is null.");
		if (!IsValid())
		{
			return;
		}

		m_Owner->FreeDescriptorInternal(*this);
		Reset();
	}

	void DX12DescriptorHandle::Retire(const DX12FencePoint& fencePoint) noexcept
	{
		GGLAB_ASSERT_MSG(m_Owner, "Retire(): Descriptor owner is null.");
		if (!IsValid())
		{
			return;
		}

		m_Owner->RetireDescriptorInternal(*this, fencePoint);
		Reset();
	}

	void DX12DescriptorHandle::Reset() noexcept
	{
		m_Index = InvalidIndex;
		m_Count = 0;
		m_Owner = nullptr;
	}

	void DX12DescriptorHandle::MoveFrom(DX12DescriptorHandle& rhs) noexcept
	{
		m_Index = rhs.m_Index;
		m_Count = rhs.m_Count;
		m_Owner = rhs.m_Owner;
		rhs.Reset();
	}
}