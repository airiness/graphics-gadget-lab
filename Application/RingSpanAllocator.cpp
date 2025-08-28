#include "Precompiled.h"
#include "RingSpanAllocator.h"

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
			GGLAB_LOG_WARN("RingSpanAllocator: Allocate(). count is 0, nothing allocated.");
			return IndexSpan{};
		}

		if (count >= m_FreeCount)
		{
			GGLAB_LOG_WARN("RingSpanAllocator: Allocate(). Don't have enough count. needed:{}, total:{}, free:{}",
				count, m_Capacity, m_FreeCount);
			return IndexSpan{};
		}

		std::lock_guard lock(m_Mutex);

		OffsetType index = 0;
		if (m_Tail >= m_Head)
		{
			OffsetType right = m_Capacity - m_Tail;
			if (count < right - (m_Head == 0 ? 1u : 0u))
			{
				index = m_Tail;
				m_Tail = (m_Tail + count) % m_Capacity ;
				return IndexSpan{ .m_Index = index, .m_Count = count };
			}

			if (m_Head > 1u && count <= (m_Head - 1u))
			{
				index = 0;
				m_Tail = count % m_Capacity;
				return IndexSpan{ .m_Index = 0, .m_Count = count };
			}

		}


		return IndexSpan();
	}

	void RingSpanAllocator::RecordRetire(IndexSpan indexSpan, VersionType version) noexcept
	{

	}

	void RingSpanAllocator::FreeCompletedVersion(VersionType version) noexcept
	{


	}
}
