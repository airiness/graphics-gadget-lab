#pragma once
#include <memory>
#include <cstdint>
#include <string_view>

namespace gglab
{
	enum class GIFieldReadinessState : uint8_t
	{
		Initializing,
		Warming,
		Settling,
		Ready,
		Unconverged,
	};

	[[nodiscard]] constexpr std::string_view GetGIFieldReadinessName(GIFieldReadinessState state) noexcept
	{
		switch (state)
		{
		case GIFieldReadinessState::Initializing: return "Initializing";
		case GIFieldReadinessState::Warming: return "Warming";
		case GIFieldReadinessState::Settling: return "Settling";
		case GIFieldReadinessState::Ready: return "Ready";
		case GIFieldReadinessState::Unconverged: return "Unconverged";
		}
		return "Initializing";
	}

	struct GIFieldReadinessSettings
	{
		// Explicit opt-out for fixed-settling diagnostics; never a quality certificate.
		bool m_Enabled = true;
		// Measured per-update change noise for the declared refresh alpha/schedule.
		// Zero selects the experimental 1% minimum; it does not claim calibration.
		float m_NoiseSigma = 0;
		float m_SteadyNoiseSigma = 0;
		float m_RefreshHysteresis = 0.5f;
	};

	struct GIFieldReadinessSnapshot
	{
		GIFieldReadinessState m_State = GIFieldReadinessState::Initializing;
		bool m_Enabled = false;
		bool m_Refreshing = true;
		bool m_Failed = false;
		uint64_t m_CompletedUpdates = 0;
		uint64_t m_ReadyUpdate = 0;
		uint32_t m_ActiveBlocks = 0;
		uint32_t m_ExcludedBlocks = 0;
		uint32_t m_QuietBlocks = 0;
		float m_RefreshNoiseEstimate = 0;
		float m_SteadyNoiseEstimate = 0;
		uint32_t m_RefreshNoiseSamples = 0;
		uint32_t m_SteadyNoiseSamples = 0;
		float m_MaximumChange = 0;
		float m_NoiseSigma = 0;
		float m_Threshold = 0.01f;
		float m_IrradianceFloor = 0.001f;
		float m_Hysteresis = 0.5f;
	};

	// Frozen before graph construction, from fence-completed measurement records.
	struct GIFieldSummary;
	struct GIFieldCaptureState
	{
		uint64_t m_Generation = 0;
		uint64_t m_SceneRevision = 0;
		uint64_t m_LightingGeneration = 0;
		uint64_t m_EnvironmentGeneration = 0;
		GIFieldReadinessSnapshot m_Readiness;
		std::shared_ptr<const GIFieldSummary> m_Summary;
	};
}
