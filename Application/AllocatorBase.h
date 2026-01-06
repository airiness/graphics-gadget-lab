#pragma once
//#include "MathUtils.h"

#include <cstdint>

namespace gglab
{
	// TODO: use concept define offset and count type.
	//template<utils::UnsignedInt OffsetType = uint32_t, utils::UnsignedInt CountType = uint32_t>
	class AllocatorBase
	{
	public:
		using OffsetType = uint32_t;
		using CountType = uint32_t;

		static constexpr OffsetType InvalidOffset = std::numeric_limits<OffsetType>::max();

		struct IndexSpan
		{
			OffsetType m_Index = InvalidOffset;
			CountType m_Count = 0;
			bool IsValid() const noexcept { return m_Index != InvalidOffset && m_Count > 0; }
		};

	public:
		explicit AllocatorBase(CountType capacity) noexcept : m_Capacity(capacity), m_FreeCount(capacity) {}
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(AllocatorBase);
		virtual ~AllocatorBase() = default;

		virtual IndexSpan Allocate(CountType count) noexcept = 0;

	protected:
		CountType m_Capacity = 0;
		CountType m_FreeCount = 0;
	};

}