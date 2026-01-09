#include "Precompiled.h"
#include "DX12DescriptorFreeListAllocator.h"
#include "DX12DescriptorTypes.h"
#include "DX12DescriptorHeap.h"

namespace gglab
{
	DX12DescriptorFreeListAllocator::DX12DescriptorFreeListAllocator(const CreateInfo& createInfo) noexcept
		: DX12DescriptorAllocatorBase(createInfo)
		, m_Allocator(createInfo.m_Range.m_Count)
		, m_Generation(createInfo.m_Range.m_Count, 1)
#if defined (BUILD_DEBUG)
		, m_Allocated(createInfo.m_Range.m_Count, 0)
#endif
	{
	}

	DX12DescriptorHandle DX12DescriptorFreeListAllocator::AllocateHandle(uint32_t count) noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();

		const auto local = m_Allocator.Allocate(count);
		if (!local.IsValid())
		{
			return {};
		}

		const auto localSpan = ToSpan(local);
		MarkAllocated(localSpan);
		const auto globalSpan = ToGlobalSpan(localSpan);
		const auto generation = (count == 1) ? m_Generation[localSpan.m_Index] : 0;

		return CreateHandleFromGlobalSpan(globalSpan, generation);
	}

	DX12DescriptorView DX12DescriptorFreeListAllocator::AllocateView() noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();

		const auto local = m_Allocator.Allocate(1);
		if (!local.IsValid())
		{
			return {};
		}

		const auto localSpan = ToSpan(local);
		MarkAllocated(localSpan);
		const auto [globalIndex, globalCount] = ToGlobalSpan(localSpan);

		return {
			.m_CpuHandle = CpuHandleAtGlobalIndex(globalIndex),
			.m_GpuHandle = IsShaderVisible() ? GpuHandleAtGlobalIndex(globalIndex) : CD3DX12_GPU_DESCRIPTOR_HANDLE{},
#if defined(BUILD_DEBUG)
			.m_DebugType = GetHeap()->Type()
#endif
		};
	}

	DX12DescriptorID DX12DescriptorFreeListAllocator::AllocateId() noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();

		const auto local = m_Allocator.Allocate(1);
		if (!local.IsValid())
		{
			return {};
		}

		const auto localSpan = ToSpan(local);
		MarkAllocated(localSpan);
		const auto [globalIndex, globalCount] = ToGlobalSpan(localSpan);

		return { globalIndex, m_Generation[localSpan.m_Index] };
	}

	void DX12DescriptorFreeListAllocator::RetireId(
		const DX12DescriptorID& descriptorId,
		const DX12FencePoint& fencePoint) noexcept
	{
		if (!descriptorId.IsValid())
		{
			return;
		}

		std::lock_guard lock(m_Mutex);

#if defined (BUILD_DEBUG)
		// DescriptorID Generation check
		const auto localIndex = ToLocalIndex(descriptorId.m_Index);
		GGLAB_ASSERT_MSG(m_Generation[localIndex] == descriptorId.m_Generation, "RetireId: stale ID. ID check failed.");
		GGLAB_ASSERT_MSG(m_Allocated[localIndex] == AllocateState::Allocated, "RetireId: ID point to a freed slot.");

#endif

		const DX12DescriptorSpan globalSpan{ descriptorId.m_Index, 1 };
		const auto localSpan = ToLocalSpan(globalSpan);
		AddPending(Pending{ localSpan, fencePoint });
	}

	void DX12DescriptorFreeListAllocator::DeferFreeFromCpuHandleInFrame(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) noexcept
	{
		std::lock_guard lock(m_Mutex);

		const uint32_t globalIndex = GlobalIndexFromCpuHandle(cpuHandle);
		const DX12DescriptorSpan globalSpan{ globalIndex, 1 };
		const DX12DescriptorSpan localSpan = ToLocalSpan(globalSpan);

#if defined	(BUILD_DEBUG)
		GGLAB_ASSERT_MSG(localSpan.m_Index < m_Allocated.size(), "DeferFree: Invalid descriptor index.");
		GGLAB_ASSERT_MSG(m_Allocated[localSpan.m_Index] != AllocateState::Free, "DeferFree: slot already free.");
		
		if (m_Allocated[localSpan.m_Index] == AllocateState::Pending)
		{
			return;
		}
		m_Allocated[localSpan.m_Index] = AllocateState::Pending;
#endif

		m_FreeInFrameSpans.push_back(localSpan);
	}

	void DX12DescriptorFreeListAllocator::DeferFreeFromGpuHandleInFrame(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) noexcept
	{
		std::lock_guard lock(m_Mutex);

		const uint32_t globalIndex = GlobalIndexFromGpuHandle(gpuHandle);
		const DX12DescriptorSpan globalSpan{ globalIndex, 1 };
		const DX12DescriptorSpan localSpan = ToLocalSpan(globalSpan);

#if defined	(BUILD_DEBUG)
		GGLAB_ASSERT_MSG(localSpan.m_Index < m_Allocated.size(), "DeferFree: Invalid descriptor index.");
		GGLAB_ASSERT_MSG(m_Allocated[localSpan.m_Index] != AllocateState::Free, "DeferFree: slot already free.");

		if (m_Allocated[localSpan.m_Index] == AllocateState::Pending)
		{
			return;
		}
		m_Allocated[localSpan.m_Index] = AllocateState::Pending;
#endif

		m_FreeInFrameSpans.push_back(localSpan);
	}

	void DX12DescriptorFreeListAllocator::Tick() noexcept
	{
		std::lock_guard lock(m_Mutex);

		FreeCompleted();
	}

	void DX12DescriptorFreeListAllocator::EndFrame(const DX12FencePoint& fencePoint) noexcept
	{
		std::lock_guard lock(m_Mutex);

		for (const auto& localSpan : m_FreeInFrameSpans)
		{
			AddPending(Pending{ localSpan, fencePoint });
		}
		m_FreeInFrameSpans.clear();
	}

	void DX12DescriptorFreeListAllocator::FreeHandleInternal(DX12DescriptorHandle& descriptorHandle) noexcept
	{
		std::lock_guard lock(m_Mutex);

		GGLAB_ASSERT_MSG(descriptorHandle.OwnerAllocator() == this, "FreeHandleInternal: wrong owner.");
		const DX12DescriptorSpan globalSpan{ descriptorHandle.Index(), descriptorHandle.Count() };
		const DX12DescriptorSpan localSpan = ToLocalSpan(globalSpan);

		FreeLocalSpanImmediately(localSpan);
	}

	void DX12DescriptorFreeListAllocator::RetireHandleInternal(const DX12DescriptorHandle& descriptorHandle, const DX12FencePoint& fencePoint) noexcept
	{
		std::lock_guard lock(m_Mutex);

		GGLAB_ASSERT_MSG(descriptorHandle.OwnerAllocator() == this, "RetireHandleInternal: wrong owner.");
		const DX12DescriptorSpan globalSpan{ descriptorHandle.Index(), descriptorHandle.Count() };
		const DX12DescriptorSpan localSpan = ToLocalSpan(globalSpan);

		AddPending(Pending{ localSpan, fencePoint });
	}

	void DX12DescriptorFreeListAllocator::FreeCompleted() noexcept
	{
		while (!m_PendingQueue.empty())
		{
			auto& front = m_PendingQueue.front();
			if (!front.m_FencePoint.IsCompleted())
			{
				break;
			}
			FreeLocalSpanImmediately(front.m_Span);
			m_PendingQueue.pop_front();
		}
	}

	void DX12DescriptorFreeListAllocator::FreeLocalSpanImmediately(const DX12DescriptorSpan& localSpan) noexcept
	{
		GGLAB_ASSERT_MSG(localSpan.IsValid(), "FreeLocalSpanImmediate: invalid span.");
		GGLAB_ASSERT_MSG(localSpan.End() <= m_Range.m_Count, "FreeLocalSpanImmediate: out of local range.");

		MarkFreed(localSpan);

		// Update generation of descriptor
		for (uint32_t index = 0; index < localSpan.m_Count; ++index)
		{
			++m_Generation[localSpan.m_Index + index];
		}

		m_Allocator.Free(AllocatorBase::IndexSpan{ .m_Index = localSpan.m_Index, .m_Count = localSpan.m_Count });
	}

	void DX12DescriptorFreeListAllocator::AddPending(const Pending& pending) noexcept
	{
#if defined (BUILD_DEBUG)
		if (!m_PendingQueue.empty())
		{
			GGLAB_ASSERT_MSG(pending.m_FencePoint.GetValue() >= m_PendingQueue.back().m_FencePoint.GetValue(),
				"Pending fence value must be non-decreasing.");
		}
#endif

		m_PendingQueue.emplace_back(pending);
	}

	void DX12DescriptorFreeListAllocator::MarkAllocated(const DX12DescriptorSpan& localSpan) noexcept
	{
#if defined (BUILD_DEBUG)
		for (uint32_t index = 0; index < localSpan.m_Count; ++index)
		{
			const auto descriptorIndex = localSpan.m_Index + index;
			GGLAB_ASSERT_MSG(descriptorIndex < m_Allocated.size(), "MarkAllocated: Invalid descriptor index.");
			GGLAB_ASSERT_MSG(m_Allocated[descriptorIndex] == AllocateState::Free, "MarkAllocated: double allocate error.");
			m_Allocated[descriptorIndex] = AllocateState::Allocated;
		}
#endif
	}

	void DX12DescriptorFreeListAllocator::MarkFreed(const DX12DescriptorSpan& localSpan) noexcept
	{
#if defined (BUILD_DEBUG)
		for (uint32_t index = 0; index < localSpan.m_Count; ++index)
		{
			const auto descriptorIndex = localSpan.m_Index + index;
			GGLAB_ASSERT_MSG(descriptorIndex < m_Allocated.size(), "MarkFreed: Invalid descriptor index.");
			GGLAB_ASSERT_MSG(m_Allocated[descriptorIndex] == AllocateState::Allocated || m_Allocated[descriptorIndex] == AllocateState::Pending,
				"MarkFreed: double free or free unallocated error.");
			m_Allocated[descriptorIndex] = AllocateState::Free;
		}
#endif
	}

	DX12DescriptorSpan DX12DescriptorFreeListAllocator::ToSpan(const AllocatorBase::IndexSpan& indexSpan) noexcept
	{
		return { .m_Index = indexSpan.m_Index, .m_Count = indexSpan.m_Count };
	}
}