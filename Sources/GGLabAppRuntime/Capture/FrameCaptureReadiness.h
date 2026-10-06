#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gglab
{
	enum class FrameCaptureGateState : uint8_t
	{
		Ready,
		Pending,
		Failed,
	};

	[[nodiscard]] constexpr std::string_view GetFrameCaptureGateStateName(
		FrameCaptureGateState state) noexcept
	{
		switch (state)
		{
		case FrameCaptureGateState::Ready:
			return "ready";
		case FrameCaptureGateState::Pending:
			return "pending";
		case FrameCaptureGateState::Failed:
			return "failed";
		}
		return "unknown";
	}

	// One named condition that must hold before an after-ready capture is taken.
	struct FrameCaptureGate
	{
		std::string m_Name;
		FrameCaptureGateState m_State = FrameCaptureGateState::Pending;
		std::string m_Detail;
	};

	// Content and runtime readiness of the frame being built. Readiness is about
	// loaded and published state only; temporal convergence is expressed
	// separately by the number of settled frames a capture requests.
	struct FrameCaptureReadiness
	{
		std::vector<FrameCaptureGate> m_Gates;

		void Add(std::string name, FrameCaptureGateState state, std::string detail = {})
		{
			m_Gates.push_back({
				.m_Name = std::move(name),
				.m_State = state,
				.m_Detail = std::move(detail),
				});
		}

		[[nodiscard]] bool IsReady() const noexcept
		{
			return std::ranges::all_of(m_Gates, [](const FrameCaptureGate& gate)
				{ return gate.m_State == FrameCaptureGateState::Ready; });
		}
		[[nodiscard]] bool HasFailed() const noexcept
		{
			return std::ranges::any_of(m_Gates, [](const FrameCaptureGate& gate)
				{ return gate.m_State == FrameCaptureGateState::Failed; });
		}
	};
}
