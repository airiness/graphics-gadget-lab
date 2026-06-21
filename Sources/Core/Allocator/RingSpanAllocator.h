#pragma once
#include "Core/Allocator/AllocatorBase.h"

namespace gglab
{
	class RingSpanAllocator : public AllocatorBase
	{
	public:
		struct AlignedAllocation
		{
			IndexSpan m_ReservedSpan{};
			OffsetType m_AlignedIndex = InvalidOffset;

			bool IsValid() const noexcept
			{
				return m_ReservedSpan.IsValid() && m_AlignedIndex != InvalidOffset;
			}
		};

	private:
		using VersionType = uint64_t;
		struct RetireRecord
		{
			IndexSpan m_IndexSpan;
			VersionType m_Version;
		};

	public:
		explicit RingSpanAllocator(CountType capacity) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(RingSpanAllocator);
		~RingSpanAllocator() override = default;

		IndexSpan Allocate(CountType count) noexcept override;
		AlignedAllocation AllocateAligned(CountType count, CountType alignment) noexcept;
		void RecordRetire(IndexSpan indexSpan, VersionType version) noexcept;
		void FreeCompletedVersion(VersionType version) noexcept;

		CountType GetCurrentUsage() const noexcept { return m_Capacity - m_FreeCount; }
		CountType GetHighWater() const noexcept { return m_HighWater; }

	private:
		OffsetType m_Head = 0;
		OffsetType m_Tail = 0;
		CountType m_HighWater = 0;

		std::deque<RetireRecord> m_RetireRecords;
	};
}
