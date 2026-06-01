#pragma once
#include "Core/Allocator/AllocatorBase.h"

namespace gglab
{
	class RingSpanAllocator : public AllocatorBase
	{
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
		void RecordRetire(IndexSpan indexSpan, VersionType version) noexcept;
		void FreeCompletedVersion(VersionType version) noexcept;

	private:
		OffsetType m_Head = 0;
		OffsetType m_Tail = 0;

		std::deque<RetireRecord> m_RetireRecords;
	};
}