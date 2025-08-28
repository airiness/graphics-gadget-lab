#include "Precompiled.h"
#include "FreeListSpanAllocator.h"

namespace gglab
{
	FreeListSpanAllocator::FreeListSpanAllocator(CountType capacity) noexcept :
		AllocatorBase(capacity)
	{
		AddBlock(0, capacity);
	}

	FreeListSpanAllocator::IndexSpan FreeListSpanAllocator::Allocate(CountType count) noexcept
	{
		if (count == 0)
		{
			GGLAB_LOG_WARN("FreeListSpanAllocator: Allocate(). count is 0, nothing allocated.");
			return IndexSpan();
		}

		const auto countMapIt = m_CountMap.lower_bound(count);
		if (countMapIt == m_CountMap.end())
		{
			GGLAB_LOG_WARN("FreeListSpanAllocator: Allocate(). Don't have enough count. needed:{}, total:{}, free:{}",
				count, m_Capacity, m_FreeCount);
			return IndexSpan();
		}

		const auto& offsetMapIt = countMapIt->second;
		const auto allocOffset = offsetMapIt->first;
		const OffsetType newOffset = allocOffset + count;
		const CountType newCount = countMapIt->first - count;

		// Erase offset map iterator first.
		m_OffsetMap.erase(offsetMapIt);
		m_CountMap.erase(countMapIt);

		if (newCount > 0)
		{
			AddBlock(newOffset, newCount);
		}
		m_FreeCount -= count;

		return IndexSpan{ .m_Index = allocOffset, .m_Count = count };
	}

	void FreeListSpanAllocator::Free(const IndexSpan& indexSpan) noexcept
	{
		const auto offset = indexSpan.m_Index;
		const auto count = indexSpan.m_Count;

		const auto nextOffsetIt = m_OffsetMap.upper_bound(offset);
		auto prevOffsetIt = nextOffsetIt;
		if (nextOffsetIt != m_OffsetMap.begin())
		{
			--prevOffsetIt;
		}
		else
		{
			prevOffsetIt = m_OffsetMap.end();
		}

		auto newOffset = offset;
		auto newCount = count;

		// Has prev free block
		if (prevOffsetIt != m_OffsetMap.end() && prevOffsetIt->first + prevOffsetIt->second.m_Count == offset)
		{
			newOffset = prevOffsetIt->first;
			newCount += prevOffsetIt->second.m_Count;

			m_CountMap.erase(prevOffsetIt->second.m_OrderByCountIt);
			m_OffsetMap.erase(prevOffsetIt);
		}

		// Has next free block
		if (nextOffsetIt != m_OffsetMap.end() && offset + count == nextOffsetIt->first)
		{
			newCount += nextOffsetIt->second.m_Count;

			m_CountMap.erase(nextOffsetIt->second.m_OrderByCountIt);
			m_OffsetMap.erase(nextOffsetIt);
		}

		AddBlock(newOffset, newCount);
	}

	void FreeListSpanAllocator::AddBlock(OffsetType offset, CountType count) noexcept
	{
		const auto offsetMapEmplaceResult = m_OffsetMap.emplace(offset, FreeBlock{ count });
		if (offsetMapEmplaceResult.second)
		{
			const auto countMapIt = m_CountMap.emplace(count, offsetMapEmplaceResult.first);
			offsetMapEmplaceResult.first->second.m_OrderByCountIt = countMapIt;
		}
	}
}