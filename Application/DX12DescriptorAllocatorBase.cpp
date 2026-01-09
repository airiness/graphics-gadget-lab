#include "Precompiled.h"
#include "DX12DescriptorAllocatorBase.h"
#include "DX12DescriptorHeap.h"

namespace gglab
{
	DX12DescriptorAllocatorBase::DX12DescriptorAllocatorBase(
		const CreateInfo& createInfo) noexcept :
		m_DescriptorHeap(createInfo.m_DescriptorHeap),
		m_Range(createInfo.m_Range),
		m_Type(m_DescriptorHeap->Type()),
		m_Flags(m_DescriptorHeap->Flags()),
		m_CpuStart(m_DescriptorHeap->CpuHandleStart()),
		m_GpuStart(m_DescriptorHeap->GpuHandleStart()),
		m_IncrementSize(m_DescriptorHeap->DescriptorIncrementSize())
	{
	}

	uint32_t DX12DescriptorAllocatorBase::ToLocalIndex(uint32_t globalIndex) const noexcept
	{
		GGLAB_ASSERT_MSG(globalIndex >= m_Range.m_Begin && globalIndex < m_Range.End(), "ToLocalIndex: out of range.");
		return globalIndex - m_Range.m_Begin;
	}

	uint32_t DX12DescriptorAllocatorBase::ToGlobalIndex(uint32_t localIndex) const noexcept
	{
		GGLAB_ASSERT_MSG(localIndex < m_Range.m_Count, "ToGlobalIndex: out of range.");
		return m_Range.m_Begin + localIndex;
	}

	DX12DescriptorSpan DX12DescriptorAllocatorBase::ToLocalSpan(const DX12DescriptorSpan& globalSpan) const noexcept
	{
		GGLAB_ASSERT_MSG(globalSpan.IsValid(), "ToLocalSpan: invalid span.");
		GGLAB_ASSERT_MSG(globalSpan.m_Index >= m_Range.m_Begin && globalSpan.End() <= m_Range.End(), "ToLocalSpan: span out of range.");
		return { .m_Index = ToLocalIndex(globalSpan.m_Index), .m_Count = globalSpan.m_Count };
	}

	DX12DescriptorSpan DX12DescriptorAllocatorBase::ToGlobalSpan(const DX12DescriptorSpan& localSpan) const noexcept
	{
		GGLAB_ASSERT_MSG(localSpan.IsValid(), "ToGlobalSpan: invalid span.");
		GGLAB_ASSERT_MSG(localSpan.End() <= m_Range.m_Count, "ToGlobalSpan: local span out of range.");
		return { .m_Index = ToGlobalIndex(localSpan.m_Index), .m_Count = localSpan.m_Count };
	}

	uint32_t DX12DescriptorAllocatorBase::GlobalIndexFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) const noexcept
	{
		GGLAB_ASSERT_MSG(cpuHandle.ptr >= m_CpuStart.ptr, "GlobalIndexFromCpuHandle: below heap start.");
		const uint64_t delta = cpuHandle.ptr - m_CpuStart.ptr;
		GGLAB_ASSERT_MSG((delta % m_IncrementSize) == 0, "GlobalIndexFromCpuHandle: unaligned handle.");
		const uint32_t global = static_cast<uint32_t>(delta / m_IncrementSize);
		GGLAB_ASSERT_MSG(global >= m_Range.m_Begin && global < m_Range.End(), "GlobalIndexFromCpuHandle: not in range.");
		return global;
	}

	uint32_t DX12DescriptorAllocatorBase::GlobalIndexFromGpuHandle(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) const noexcept
	{
		GGLAB_ASSERT_MSG(IsShaderVisible(), "GlobalIndexFromGpuHandle: heap not shader-visible.");
		GGLAB_ASSERT_MSG(gpuHandle.ptr >= m_GpuStart.ptr, "GlobalIndexFromGpuHandle: below heap start.");
		const uint64_t delta = gpuHandle.ptr - m_GpuStart.ptr;
		GGLAB_ASSERT_MSG((delta % m_IncrementSize) == 0, "GlobalIndexFromGpuHandle: unaligned handle.");
		const uint32_t global = static_cast<uint32_t>(delta / m_IncrementSize);
		GGLAB_ASSERT_MSG(global >= m_Range.m_Begin && global < m_Range.End(), "GlobalIndexFromGpuHandle: not in range.");
		return global;
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE DX12DescriptorAllocatorBase::CpuHandleAtGlobalIndex(uint32_t globalIndex) const noexcept
	{
		GGLAB_ASSERT_MSG(globalIndex < m_DescriptorHeap->DescriptorCount(), "CpuHandleAtGlobalIndex: out of heap.");
		return { m_CpuStart, static_cast<INT>(globalIndex), static_cast<UINT>(m_IncrementSize) };
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE DX12DescriptorAllocatorBase::GpuHandleAtGlobalIndex(uint32_t globalIndex) const noexcept
	{
		if (!IsShaderVisible())
		{
			GGLAB_LOG_GRAPHICS_WARN("DescriptorAllocator: GetGpuHandle from shader invisible heap.");
			return {};
		}

		GGLAB_ASSERT_MSG(globalIndex < m_DescriptorHeap->DescriptorCount(), "GpuHandleAtGlobalIndex: out of heap.");
		return { m_GpuStart, static_cast<INT>(globalIndex), static_cast<UINT>(m_IncrementSize) };
	}

	DX12DescriptorHandle DX12DescriptorAllocatorBase::CreateHandleFromGlobalSpan(const DX12DescriptorSpan& globalSpan, uint32_t generation) noexcept
	{
		if (!globalSpan.IsValid())
		{
			return {};
		}

		GGLAB_ASSERT_MSG(globalSpan.m_Index >= m_Range.m_Begin && globalSpan.End() <= m_Range.End(), "CreateHandleFromGlobalSpan: span out of range.");

		DX12DescriptorHandle h{};
		h.m_Index = globalSpan.m_Index;
		h.m_Count = globalSpan.m_Count;
		h.m_Generation = generation;
		h.m_OwnerAllocator = this;
		return h;
	}
}