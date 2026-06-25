#pragma once
#include "Graphics/RHI/RHIHandles.h"
#include "Graphics/RHI/RHITypes.h"

#include <cstdint>
#include <tuple>

namespace gglab
{
	struct RHIFencePoint
	{
		RHIFenceHandle m_Fence{};
		uint64_t m_Value = 0;

		constexpr RHIFencePoint() noexcept = default;
		constexpr RHIFencePoint(RHIFenceHandle fence, uint64_t value) noexcept :
			m_Fence(fence),
			m_Value(value)
		{}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Fence.IsValid() && m_Value != 0;
		}

		constexpr void Reset() noexcept
		{
			m_Fence.Reset();
			m_Value = 0;
		}

		[[nodiscard]] constexpr auto AsTuple() const noexcept
		{
			return std::tie(m_Fence, m_Value);
		}

		explicit operator bool() const noexcept { return IsValid(); }

		bool operator==(const RHIFencePoint&) const noexcept = default;
		bool operator<(const RHIFencePoint& other) const noexcept
		{
			return AsTuple() < other.AsTuple();
		}
	};
}
