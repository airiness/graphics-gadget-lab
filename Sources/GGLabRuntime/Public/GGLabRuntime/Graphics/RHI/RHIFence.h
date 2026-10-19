#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

#include <cstdint>
#include <string_view>
#include <tuple>

namespace gglab
{
	// Non-blocking completion query. A decision that verifies quiescence or accepts
	// GPU-written contents counts only Completed.
	enum class RHIFenceQueryResult : uint8_t
	{
		Pending,    // The query succeeded; the value has not been reached yet.
		Completed,  // The value was reached, and the device is not removed or lost.
		DeviceLost, // The native API confirms device removal or loss.
		Failed,     // Any other native failure, or an invalid or unknown fence point.
	};

	// Blocking CPU wait or idle wait.
	enum class RHIGpuWaitResult : uint8_t
	{
		Completed,
		Timeout,    // The wait expired; this says nothing about the device.
		DeviceLost, // Confirmed by the native API.
		Failed,
	};

	inline constexpr uint32_t RHIInfiniteWaitMilliseconds = UINT32_MAX;

	// Native-confirmed loss permits destruction, never acceptance of GPU-written data.
	[[nodiscard]] constexpr bool IsFenceSafeToRelease(RHIFenceQueryResult result) noexcept
	{
		return result == RHIFenceQueryResult::Completed || result == RHIFenceQueryResult::DeviceLost;
	}

	[[nodiscard]] constexpr RHIGpuWaitResult ToRHIGpuWaitResult(RHIFenceQueryResult query) noexcept
	{
		switch (query)
		{
		case RHIFenceQueryResult::Completed:
			return RHIGpuWaitResult::Completed;
		case RHIFenceQueryResult::DeviceLost:
			return RHIGpuWaitResult::DeviceLost;
		case RHIFenceQueryResult::Pending:
			return RHIGpuWaitResult::Timeout;
		case RHIFenceQueryResult::Failed:
			break;
		}
		return RHIGpuWaitResult::Failed;
	}

	[[nodiscard]] constexpr std::string_view GetRHIGpuWaitResultName(RHIGpuWaitResult result) noexcept
	{
		switch (result)
		{
		case RHIGpuWaitResult::Completed:
			return "Completed";
		case RHIGpuWaitResult::Timeout:
			return "Timeout";
		case RHIGpuWaitResult::DeviceLost:
			return "DeviceLost";
		case RHIGpuWaitResult::Failed:
			break;
		}
		return "Failed";
	}

	struct RHIFencePoint
	{
		RHIFenceHandle m_Fence{};
		uint64_t m_Value = 0;

		constexpr RHIFencePoint() noexcept = default;
		constexpr RHIFencePoint(RHIFenceHandle fence, uint64_t value) noexcept :
			m_Fence(fence), m_Value(value)
		{
		}

		[[nodiscard]] bool IsValid() const noexcept { return m_Fence.IsValid() && m_Value != 0; }

		constexpr void Reset() noexcept
		{
			m_Fence.Reset();
			m_Value = 0;
		}

		[[nodiscard]] constexpr auto AsTuple() const noexcept { return std::tie(m_Fence, m_Value); }

		explicit operator bool() const noexcept { return IsValid(); }

		bool operator==(const RHIFencePoint&) const noexcept = default;
		bool operator<(const RHIFencePoint& other) const noexcept
		{
			return AsTuple() < other.AsTuple();
		}
	};
}
