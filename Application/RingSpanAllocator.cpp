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
			return {};
		}

		OffsetType index = 0;
		if (m_Tail >= m_Head)
		{
			OffsetType right = m_Capacity - m_Tail;
			if (count <= right - (m_Head == 0 ? 1u : 0u))
			{
				index = m_Tail;
				m_Tail = (m_Tail + count) % m_Capacity;
				m_FreeCount -= count;
				return IndexSpan{ .m_Index = index, .m_Count = count };
			}

			if (m_Head > 1 && count <= (m_Head - 1))
			{
				index = 0;
				m_Tail = count % m_Capacity;
				m_FreeCount -= count;
				return IndexSpan{ .m_Index = 0, .m_Count = count };
			}
		}

		const auto available = m_Head - m_Tail - 1;
		if (count <= available)
		{
			index = m_Tail;
			m_Tail = (m_Tail + count) % m_Capacity;
			m_FreeCount -= count;
			return IndexSpan{ .m_Index = index, .m_Count = count };
		}

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
			if (m_Head == front.m_IndexSpan.m_Index)
			{
				m_Head = (front.m_IndexSpan.m_Index + front.m_IndexSpan.m_Count) % m_Capacity;
			}

			m_FreeCount += front.m_IndexSpan.m_Count;
			m_RetireRecords.pop_front();
		}
	}
}
