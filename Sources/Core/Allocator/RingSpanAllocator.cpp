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
		return AllocateAligned(count, 1).m_ReservedSpan;
	}

	RingSpanAllocator::AlignedAllocation RingSpanAllocator::AllocateAligned(
		CountType count,
		CountType alignment) noexcept
	{
		if (count == 0)
		{
			GGLAB_LOG_WARN("RingSpanAllocator: AllocateAligned failed. count is 0, nothing allocated.");
			return {};
		}

		alignment = std::max(alignment, CountType{ 1 });
		if (count > m_FreeCount)
		{
			GGLAB_LOG_WARN("RingSpanAllocator: AllocateAligned failed. request={}, free={}",
				count, m_FreeCount);
			return {};
		}

		// Empty rings have no live position to preserve. Normalizing to zero
		// avoids consuming the suffix merely to satisfy a large alignment.
		if (m_FreeCount == m_Capacity)
		{
			m_Head = 0;
			m_Tail = 0;
		}

		const auto alignUp = [alignment](OffsetType value) noexcept
			{
				const auto remainder = value % alignment;
				return remainder ? value + alignment - remainder : value;
			};

		const auto reserve = [this](
			OffsetType reservedIndex,
			CountType reservedCount,
			OffsetType alignedIndex) noexcept
			{
				m_Tail = static_cast<OffsetType>((reservedIndex + reservedCount) % m_Capacity);
				m_FreeCount -= reservedCount;
				m_HighWater = std::max(m_HighWater, GetCurrentUsage());
				GGLAB_ASSERT(m_HighWater <= m_Capacity);
				return AlignedAllocation{
					.m_ReservedSpan = {
						.m_Index = reservedIndex,
						.m_Count = reservedCount,
					},
					.m_AlignedIndex = alignedIndex,
				};
			};

		if (m_Tail >= m_Head)
		{
			const OffsetType right = m_Capacity - m_Tail;

			// Try to allocate at [tail, capacity).
			const OffsetType alignedIndex = alignUp(m_Tail);
			if (alignedIndex < m_Capacity)
			{
				const CountType padding = alignedIndex - m_Tail;
				const CountType reservedCount = padding + count;
				if (reservedCount <= right && reservedCount <= m_FreeCount)
				{
					return reserve(m_Tail, reservedCount, alignedIndex);
				}
			}

			// Wrap to zero. The unused suffix must belong to this reservation;
			// otherwise tail can become equal to head while the ring is only
			// partially occupied, making the next allocation overlap live data.
			const CountType wrappedReservedCount = right + count;
			if (m_Head > 0 &&
				count <= m_Head &&
				wrappedReservedCount <= m_FreeCount)
			{
				return reserve(m_Tail, wrappedReservedCount, 0);
			}
		}

		// Tail is on the left of head; free range is [tail, head).
		if (m_Tail < m_Head)
		{
			const OffsetType alignedIndex = alignUp(m_Tail);
			const CountType padding = alignedIndex - m_Tail;
			const CountType reservedCount = padding + count;
			const CountType available = m_Head - m_Tail;
			if (reservedCount <= available && reservedCount <= m_FreeCount)
			{
				return reserve(m_Tail, reservedCount, alignedIndex);
			}
		}

		GGLAB_LOG_WARN(
			"RingSpanAllocator::AllocateAligned failed: no contiguous space. request={}, alignment={}, free={}.",
			count,
			alignment,
			m_FreeCount);
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
			GGLAB_ASSERT(m_FreeCount <= m_Capacity);
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
