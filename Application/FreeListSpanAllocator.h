#pragma once
#include "AllocatorBase.h"

namespace gglab
{
	class FreeListSpanAllocator : public AllocatorBase
	{
	private:
		struct FreeBlock;
		using FreeBlocksByOffset = std::map<OffsetType, FreeBlock>;
		using FreeBlocksByCount = std::multimap<CountType, FreeBlocksByOffset::iterator>;
		struct FreeBlock
		{
			explicit FreeBlock(CountType count) : m_Count(count) {}
			CountType m_Count;
			FreeBlocksByCount::iterator m_OrderByCountIt;
		};

	public:
		explicit FreeListSpanAllocator(CountType capacity) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(FreeListSpanAllocator);
		~FreeListSpanAllocator() override = default;

		IndexSpan Allocate(CountType count) noexcept override;
		void Free(const IndexSpan& indexSpan) noexcept;

	private:
		void AddBlock(OffsetType offset, CountType count) noexcept;

	private:
		FreeBlocksByOffset m_OffsetMap;
		FreeBlocksByCount m_CountMap;
	};
}