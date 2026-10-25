#pragma once

#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"

#include <cstdint>
#include <optional>

namespace gglab
{
	struct DemoFrameFeedback;
	struct GpuProfileFrameSnapshot;

	inline constexpr uint32_t TemporalAALabGpuTimingSampleTarget = 120;

	struct TemporalAALabGpuTimingDomain
	{
		TemporalAASettings m_Settings{};
		ViewResolution m_Resolution{};
		RenderViewID m_DisplayViewId = RenderViewID::Unknown;
		uint64_t m_ResetIdentity = 0;
		uint64_t m_SessionIdentity = 0;
		uint32_t m_FrameSlotCount = 0;

		bool operator==(const TemporalAALabGpuTimingDomain&) const noexcept = default;
	};

	struct TemporalAALabGpuTimingSnapshot
	{
		std::optional<TemporalAALabGpuTimingDomain> m_Domain;
		uint64_t m_FirstDomainFrameSerial = 0;
		uint64_t m_LastSubmittedFrameSerial = 0;
		uint64_t m_FirstEligibleFrameSerial = 0;
		uint64_t m_LastGpuFrameIndex = 0;
		uint64_t m_FirstSampleFrameSerial = 0;
		uint64_t m_LastSampleFrameSerial = 0;
		uint32_t m_WarmupFrameCount = 0;
		uint32_t m_WarmupFramesRemaining = 0;
		uint32_t m_SampleCount = 0;
		double m_SumMilliseconds = 0.0;
		double m_MinMilliseconds = 0.0;
		double m_MaxMilliseconds = 0.0;
	};

	// Lab-owned policy over submitted CPU frame values and completed GPU profiles.
	// It owns no GPU resource and never infers frame identity from readback latency.
	class TemporalAALabGpuTimingWindow
	{
	public:
		void Reset() noexcept { m_State = {}; }
		void OnFrameSubmitted(const DemoFrameFeedback& feedback,
			uint32_t frameSlotCount, bool profilingEnabled) noexcept;
		// True only when this profile completes the window for the first time.
		[[nodiscard]] bool Capture(const GpuProfileFrameSnapshot& profile) noexcept;
		[[nodiscard]] const TemporalAALabGpuTimingSnapshot& GetSnapshot() const noexcept
		{
			return m_State;
		}

	private:
		TemporalAALabGpuTimingSnapshot m_State{};
	};
}
