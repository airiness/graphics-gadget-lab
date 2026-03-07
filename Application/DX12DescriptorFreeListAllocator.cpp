#include "Precompiled.h"
#include "DX12DescriptorFreeListAllocator.h"
#include "DX12DescriptorTypes.h"
#include "DX12DescriptorHeap.h"

namespace gglab
{
	DX12DescriptorFreeListAllocator::DX12DescriptorFreeListAllocator(const CreateInfo& createInfo) noexcept :
		DX12DescriptorAllocatorBase(createInfo),
		m_Allocator(createInfo.m_Range.m_Count),
		m_Generation(createInfo.m_Range.m_Count, 1),
		m_SlotStates(createInfo.m_Range.m_Count, SlotState::Free)
	{
		m_FreeInFrameSpans.reserve(FreeInFrameSpansReserveSize);
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

		if (!TryMarkAllocated(localSpan))
		{
			m_Allocator.Free(local);
			return {};
		}

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
		if (!TryMarkAllocated(localSpan))
		{
			m_Allocator.Free(local);
			return {};
		}

		const auto globalSpan = ToGlobalSpan(localSpan);
		const auto globalIndex = globalSpan.m_Index;

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
		if (!TryMarkAllocated(localSpan))
		{
			m_Allocator.Free(local);
			return {};
		}

		const auto [globalIndex, globalCount] = ToGlobalSpan(localSpan);

		return { .m_Index = globalIndex, .m_Generation = m_Generation[localSpan.m_Index] };
	}

	bool DX12DescriptorFreeListAllocator::IsIdAlive(const DX12DescriptorID& descriptorId) const noexcept
	{
		if (!descriptorId.IsValid())
		{
			return false;
		}

		std::lock_guard lock(m_Mutex);
		if (!ContainsGlobalIndex(descriptorId.m_Index))
		{
			return false;
		}

		const auto localIndex = ToLocalIndex(descriptorId.m_Index);

		// generation mismatch => stale
		if (m_Generation[localIndex] != descriptorId.m_Generation)
		{
			return false;
		}

		return m_SlotStates[localIndex] == SlotState::Allocated;
	}

	DX12DescriptorView DX12DescriptorFreeListAllocator::ViewAtId(const DX12DescriptorID& descriptorId) const noexcept
	{
		if (!descriptorId.IsValid())
		{
			return {};
		}

		std::lock_guard lock(m_Mutex);
		if (!ContainsGlobalIndex(descriptorId.m_Index))
		{
#if defined(BUILD_DEBUG)
			GGLAB_ASSERT_MSG(false, "ViewAtId: descriptor id not in allocator range.");
#endif
			return {};
		}

		const uint32_t localIndex = ToLocalIndex(descriptorId.m_Index);

		if (m_Generation[localIndex] != descriptorId.m_Generation)
		{
#if defined(BUILD_DEBUG)
			GGLAB_ASSERT_MSG(false, "ViewAtId: stale descriptor id (generation mismatch).");
#endif
			return {};
		}

		if (m_SlotStates[localIndex] != SlotState::Allocated)
		{
#if defined(BUILD_DEBUG)
			GGLAB_ASSERT_MSG(false, "ViewAtId: descriptor id is not currently allocated.");
#endif
			return {};
		}

		return ViewAtGlobalIndex(descriptorId.m_Index);
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

		if (!ContainsGlobalIndex(descriptorId.m_Index))
		{
#if defined(BUILD_DEBUG)
			GGLAB_ASSERT_MSG(false, "RetireId: id not in allocator range.");
#endif
			return;
		}

		const auto localIndex = ToLocalIndex(descriptorId.m_Index);
		// DescriptorID Generation check
		if (m_Generation[localIndex] != descriptorId.m_Generation)
		{
#if defined(BUILD_DEBUG)
			GGLAB_ASSERT_MSG(false, "RetireId: stale ID (generation mismatch).");
#endif
			return;
		}

		const DX12DescriptorSpan localSpan{ localIndex, 1 };
		if (!TryMarkPending(localSpan))
		{
			return;
		}

		AddPending(Pending{ localSpan, fencePoint });
	}

	void DX12DescriptorFreeListAllocator::DeferFreeFromCpuHandleInFrame(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) noexcept
	{
		if (cpuHandle.ptr == 0)
		{
			return;
		}

		std::lock_guard lock(m_Mutex);

		const uint32_t globalIndex = GlobalIndexFromCpuHandle(cpuHandle);
		DeferFreeFromGlobalIndexInFrame(globalIndex);
	}

	void DX12DescriptorFreeListAllocator::DeferFreeFromGpuHandleInFrame(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) noexcept
	{
		if (gpuHandle.ptr == 0)
		{
			return;
		}

		std::lock_guard lock(m_Mutex);

		const uint32_t globalIndex = GlobalIndexFromGpuHandle(gpuHandle);
		DeferFreeFromGlobalIndexInFrame(globalIndex);
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

		for (uint32_t index = 0; index < localSpan.m_Count; ++index)
		{
			const auto descriptorIndex = localSpan.m_Index + index;
			if (m_SlotStates[descriptorIndex] == SlotState::Pending)
			{
#if defined(BUILD_DEBUG)
				GGLAB_ASSERT_MSG(false, "FreeHandleInternal: freeing a pending span. Use Retire() instead.");
#endif
				return;
			}
		}

		FreeLocalSpanImmediately(localSpan);
	}

	void DX12DescriptorFreeListAllocator::RetireHandleInternal(const DX12DescriptorHandle& descriptorHandle, const DX12FencePoint& fencePoint) noexcept
	{
		std::lock_guard lock(m_Mutex);

		GGLAB_ASSERT_MSG(descriptorHandle.OwnerAllocator() == this, "RetireHandleInternal: wrong owner.");
		const DX12DescriptorSpan globalSpan{ descriptorHandle.Index(), descriptorHandle.Count() };
		const DX12DescriptorSpan localSpan = ToLocalSpan(globalSpan);

		if (!TryMarkPending(localSpan))
		{
			return;
		}

		AddPending(Pending{ localSpan, fencePoint });
	}

	bool DX12DescriptorFreeListAllocator::TryMarkAllocated(const DX12DescriptorSpan& localSpan) noexcept
	{
		GGLAB_ASSERT_MSG(localSpan.IsValid(), "TryMarkAllocated: invalid span.");
		GGLAB_ASSERT_MSG(localSpan.End() <= m_Range.m_Count, "TryMarkAllocated: out of range.");

		for (uint32_t index = 0; index < localSpan.m_Count; ++index)
		{
			const auto descriptorIndex = localSpan.m_Index + index;
			if (m_SlotStates[descriptorIndex] != SlotState::Free)
			{
#if defined(BUILD_DEBUG)
				GGLAB_ASSERT_MSG(false, "TryMarkAllocated: double allocate or allocator corruption.");
#endif
				return false;
			}
		}

		for (uint32_t index = 0; index < localSpan.m_Count; ++index)
		{
			m_SlotStates[localSpan.m_Index + index] = SlotState::Allocated;
		}
		return true;
	}

	bool DX12DescriptorFreeListAllocator::TryMarkPending(const DX12DescriptorSpan& localSpan) noexcept
	{
		GGLAB_ASSERT_MSG(localSpan.IsValid(), "TryMarkPending: invalid span.");
		GGLAB_ASSERT_MSG(localSpan.End() <= m_Range.m_Count, "TryMarkPending: out of range.");

		bool anyAllocated = false;
		bool anyPending = false;

		for (uint32_t index = 0; index < localSpan.m_Count; ++index)
		{
			const auto state = m_SlotStates[localSpan.m_Index + index];
			if (state == SlotState::Allocated)
			{
				anyAllocated = true;
			}
			else if (state == SlotState::Pending)
			{
				anyPending = true;
			}
			else
			{
#if defined(BUILD_DEBUG)
				GGLAB_ASSERT_MSG(false, "TryMarkPending: pending a freed slot.");
#endif
				return false;
			}
		}

		if (anyAllocated && anyPending)
		{
#if defined(BUILD_DEBUG)
			GGLAB_ASSERT_MSG(false, "TryMarkPending: mixed Allocated/Pending state (overlap).");
#endif
			return false;
		}

		if (anyPending && !anyAllocated)
		{
			// already pending
			return false;
		}

		// all allocated -> pending
		for (uint32_t index = 0; index < localSpan.m_Count; ++index)
		{
			m_SlotStates[localSpan.m_Index + index] = SlotState::Pending;
		}
		return true;
	}

	bool DX12DescriptorFreeListAllocator::MarkFreed(const DX12DescriptorSpan& localSpan) noexcept
	{
		GGLAB_ASSERT_MSG(localSpan.IsValid(), "MarkFreed: invalid span.");
		GGLAB_ASSERT_MSG(localSpan.End() <= m_Range.m_Count, "MarkFreed: out of range.");

		// Allow Allocated or Pending into Free
		for (uint32_t index = 0; index < localSpan.m_Count; ++index)
		{
			const auto state = m_SlotStates[localSpan.m_Index + index];
			if (state != SlotState::Allocated && state != SlotState::Pending)
			{
#if defined(BUILD_DEBUG)
				GGLAB_ASSERT_MSG(false, "MarkFreed: double free or freeing unallocated slot.");
#endif
				return false;
			}
		}

		for (uint32_t index = 0; index < localSpan.m_Count; ++index)
		{
			m_SlotStates[localSpan.m_Index + index] = SlotState::Free;
		}

		return true;
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

		if (!MarkFreed(localSpan))
		{
			return;
		}

		// Update generation of descriptor
		for (uint32_t index = 0; index < localSpan.m_Count; ++index)
		{
			++m_Generation[localSpan.m_Index + index];
		}

		m_Allocator.Free(AllocatorBase::IndexSpan{ .m_Index = localSpan.m_Index, .m_Count = localSpan.m_Count });
	}

	void DX12DescriptorFreeListAllocator::DeferFreeFromGlobalIndexInFrame(uint32_t globalIndex) noexcept
	{
		const DX12DescriptorSpan globalSpan{ .m_Index = globalIndex, .m_Count = 1 };
		const DX12DescriptorSpan localSpan = ToLocalSpan(globalSpan);

		// Allocated -> Pending, skip duplicate
		if (!TryMarkPending(localSpan))
		{
			return;
		}

		m_FreeInFrameSpans.push_back(localSpan);
	}

	DX12DescriptorSpan DX12DescriptorFreeListAllocator::ToSpan(const AllocatorBase::IndexSpan& indexSpan) noexcept
	{
		return { .m_Index = indexSpan.m_Index, .m_Count = indexSpan.m_Count };
	}
}