#pragma once

namespace gglab
{
	class FreeListSpanAllocator
	{
	public:
		using OffsetType = uint32_t;
		using CountType = uint32_t;

		static constexpr OffsetType InvalidOffset = std::numeric_limits<OffsetType>::max();

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
		explicit FreeListSpanAllocator(CountType totalCount) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(FreeListSpanAllocator);
		~FreeListSpanAllocator() = default;

		OffsetType Allocate(CountType count = 1) noexcept;
		void Free(OffsetType offset, CountType count) noexcept;

	private:
		void AddBlock(OffsetType offset, CountType count) noexcept;

	private:
		CountType m_TotalCount = 0;
		CountType m_FreeCount = 0;

		FreeBlocksByOffset m_OffsetMap;
		FreeBlocksByCount m_CountMap;

		std::mutex m_Mutex;
	};
}