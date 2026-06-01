#include "Core/Precompiled.h"
#include "Core/Allocator/RingSpanAllocator.h"

namespace gglab
{
	RingSpanAllocator::RingSpanAllocator(CountType capacity) noexcept :
		AllocatorBase(capacity)
	{
	}

	RingSpanAllocator::IndexSpan RingSpanAllocator::Allocate(CountType count) noexcept
	{
		if (count == 0)
		{
			GGLAB_LOG_WARN("RingSpanAllocator: Allocate failed. count is 0, nothing allocated.");
			return {};
		}

		if (count > m_FreeCount)
		{
			GGLAB_LOG_WARN("RingSpanAllocator: Allocate failed. request={}, free={}",
				count, m_FreeCount);
			return {};
		}

		OffsetType index = 0;
		if (m_Tail >= m_Head)
		{
			const OffsetType right = m_Capacity - m_Tail;

			// try to allocate at [tail, capacity)
			const OffsetType guard = (m_Head == 0 ? 1u : 0u);
			if (count <= right - guard)
			{
				index = m_Tail;
				m_Tail = static_cast<OffsetType>((m_Tail + count) % m_Capacity);
				m_FreeCount -= count;
				return IndexSpan{ .m_Index = index, .m_Count = count };
			}

			// try ao allocate at [0, head)
			if (m_Head > 1 && count <= (m_Head - 1))
			{
				index = 0;
				m_Tail = static_cast<OffsetType>(count % m_Capacity);
				m_FreeCount -= count;
				return IndexSpan{ .m_Index = index, .m_Count = count };
			}
		}

		// tail is on left of head, free range is (tail, head)
		const auto available = static_cast<CountType>(m_Head - m_Tail - 1);
		if (count <= available)
		{
			index = m_Tail;
			m_Tail = static_cast<OffsetType>((m_Tail + count) % m_Capacity);
			m_FreeCount -= count;
			return IndexSpan{ .m_Index = index, .m_Count = count };
		}

		GGLAB_LOG_WARN(
			"RingSpanAllocator::Allocate failed: no contiguous space. request = {}, free = {}.",
			count, m_FreeCount);
		return {};
	}

	void RingSpanAllocator::RecordRetire(IndexSpan indexSpan, VersionType version) noexcept
	{
		if (!indexSpan.IsValid())
		{
			return;
		}

		m_RetireRecords.emplace_back(indexSpan, version);
	}

	void RingSpanAllocator::FreeCompletedVersion(VersionType version) noexcept
	{
		while (!m_RetireRecords.empty())
		{
			auto& front = m_RetireRecords.front();
			if (front.m_Version > version)
			{
				break;
			}

			// Free completed
			m_FreeCount += front.m_IndexSpan.m_Count;
			m_RetireRecords.pop_front();

			if (!m_RetireRecords.empty())
			{
				m_Head = m_RetireRecords.front().m_IndexSpan.m_Index;
			}
			else
			{
				m_Head = m_Tail;
			}
		}
	}
}
