#pragma once
#include <memory>
#include <array>
#include <optional>
#include <vector>
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

	// Why a readiness epoch refreshes the field. A hard reset starts every history
	// empty; a lighting refresh keeps the atlas while its lighting is stale.
	enum class GIFieldRefreshReason : uint8_t
	{
		HardReset,
		Lighting,
	};

	[[nodiscard]] constexpr std::string_view GetGIFieldRefreshReasonName(GIFieldRefreshReason reason) noexcept
	{
		switch (reason)
		{
		case GIFieldRefreshReason::HardReset: return "hardReset";
		case GIFieldRefreshReason::Lighting: return "lighting";
		}
		return "hardReset";
	}

	// How a refresh integrates irradiance after its entry update, which always
	// reweights at the refresh hysteresis.
	enum class GIFieldRefreshIntegration : uint8_t
	{
		// Every refresh update reweights the history at the refresh hysteresis.
		Rapid,
		// Real history mass grows at the field's steady decay.
		Progressive,
	};

	[[nodiscard]] constexpr std::string_view GetGIFieldRefreshIntegrationName(
		GIFieldRefreshIntegration integration) noexcept
	{
		switch (integration)
		{
		case GIFieldRefreshIntegration::Rapid: return "rapid";
		case GIFieldRefreshIntegration::Progressive: return "progressive";
		}
		return "rapid";
	}

	// Per-update change RMS measured on one refresh reason's own schedule: the maximum
	// over probes of the RMS change in the calibration window of each phase. Zero
	// selects the experimental 1% minimum; it does not claim calibration.
	struct GIFieldReadinessCalibration
	{
		float m_RefreshSigma = 0;
		float m_SteadySigma = 0;
	};

	struct GIFieldReadinessSettings
	{
		// Explicit opt-out for fixed-settling diagnostics; never a quality certificate.
		bool m_Enabled = true;
		// Each reason integrates on its own schedule, so each gates with its own
		// measurement; one reason's values never stand in for the other's.
		GIFieldReadinessCalibration m_HardReset;
		GIFieldReadinessCalibration m_Lighting;
		float m_RefreshHysteresis = 0.5f;
		// A hard reset has no history to discount, so its real mass grows from zero.
		GIFieldRefreshIntegration m_HardResetIntegration = GIFieldRefreshIntegration::Progressive;
		// A lighting refresh discounts stale lighting. Progressive lighting refresh is
		// experimental: its retained old lighting overshoots a light step.
		GIFieldRefreshIntegration m_LightingIntegration = GIFieldRefreshIntegration::Rapid;
	};

	[[nodiscard]] constexpr const GIFieldReadinessCalibration& GetGIFieldReadinessCalibration(
		const GIFieldReadinessSettings& settings, GIFieldRefreshReason reason) noexcept
	{
		return reason == GIFieldRefreshReason::Lighting ? settings.m_Lighting : settings.m_HardReset;
	}

	[[nodiscard]] constexpr GIFieldRefreshIntegration GetGIFieldRefreshIntegration(
		const GIFieldReadinessSettings& settings, GIFieldRefreshReason reason) noexcept
	{
		return reason == GIFieldRefreshReason::Lighting ?
			settings.m_LightingIntegration :
			settings.m_HardResetIntegration;
	}

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

	// Multiple reasons may apply. Excluded blocks carry no statistics or blockers.
	enum class GIFieldReadinessBlocker : uint32_t
	{
		Warming = 1,
		Percentile = 2,
		Maximum = 4,
		QuietHold = 8,
	};

	struct GIFieldProbeMeasurement
	{
		float m_Change = 0;
		bool m_Active = false;
	};

	struct GIFieldBlockMeasurement
	{
		uint32_t m_ActiveProbes = 0;
		std::optional<float> m_Percentile95;
		std::optional<float> m_Maximum;
		uint32_t m_QuietUpdates = 0;
		uint32_t m_Blockers = 0;
		bool m_WarmingMet = false;
		// Threshold quietness is independent of warming and the accumulated hold.
		bool m_CurrentQuiet = false;
	};

	// Updates retained by an opt-in readiness trace, equal to the Unconverged timeout.
	inline constexpr uint32_t GIFieldReadinessTraceUpdates = 240;

	// One estimator update of an opt-in diagnostic trace: its final snapshot and block
	// decisions, without per-probe values. Records never change once published.
	struct GIFieldReadinessTraceUpdate
	{
		GIFieldReadinessSnapshot m_Readiness;
		bool m_RefreshMode = false;
		std::vector<GIFieldBlockMeasurement> m_Blocks;
		// The preceding update of the same readiness epoch; null for its first update.
		std::shared_ptr<const GIFieldReadinessTraceUpdate> m_Previous;
	};

	// Owned CPU values from one successful estimator observation. No GPU owners.
	struct GIFieldReadinessMeasurement
	{
		std::array<uint32_t, 3> m_Counts{};
		uint32_t m_BlockWidth = 0;
		uint32_t m_WarmingUpdates = 0;
		uint32_t m_RequiredQuietUpdates = 0;
		bool m_RefreshMode = false;
		bool m_IncludesDistance = false;
		GIFieldReadinessSnapshot m_Readiness;
		std::vector<GIFieldProbeMeasurement> m_Probes;
		std::vector<GIFieldBlockMeasurement> m_Blocks;
		// Newest record of the opt-in trace since this epoch's reset, or null when
		// disabled. It is this update until the trace window ends, then the last kept.
		std::shared_ptr<const GIFieldReadinessTraceUpdate> m_Trace;
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
