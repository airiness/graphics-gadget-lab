#pragma once
#include "Graphics/RenderFrameOutcome.h"
#include "Graphics/GIShaderConstants.h"
#include "GGLabRuntime/Graphics/Pipeline/GIUpdateStatus.h"
#include "GGLabRuntime/Graphics/Pipeline/GIFieldReadiness.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace gglab
{
	// Two float4 records per probe, following one float4 field header on the GPU.
	struct GIFieldProbeChange
	{
		std::array<float, 4> m_RecentChanges{};
		float m_Luminance = 0;
		float m_Change = 0;
		float m_Active = 0;
		uint32_t m_Updates = 0;
	};
	static_assert(sizeof(GIFieldProbeChange) == 32);

	struct GIFieldIntegrationHistory
	{
		double m_Weight = 0;
		float m_Hysteresis = 0;
		bool m_Refreshing = false;

		// Preview only. Publish the returned mass with the matching Submitted update.
		[[nodiscard]] GIFieldIntegrationHistory Next(float decay, bool refreshing) const noexcept
		{
			const double maximumWeight = 1.0 / (1.0 - decay);
			// A refresh keeps the atlas but deliberately reduces its old lighting's
			// influence. Distance has its own history and never enters a lighting refresh.
			const double previous = refreshing && !m_Refreshing && m_Weight > 0 ?
				maximumWeight :
				std::min(m_Weight, maximumWeight);
			const double retained = decay * previous;
			const double weight = retained + 1;
			// A short 0.5 refresh history must not acquire the full mass of a 0.998
			// steady filter. Normalize real integration weight, not fictitious samples.
			return { .m_Weight = weight, .m_Hysteresis = static_cast<float>(retained / weight),
				.m_Refreshing = refreshing };
		}
	};

	// Nominal decays for one submitted update. The policy follows the refresh reason
	// alone; no fixture, seed or backend selects it.
	struct GIFieldUpdateDecays
	{
		float m_Irradiance = 0;
		float m_Distance = 0;
		// Only a hard reset refreshes distance; a lighting refresh keeps its history.
		bool m_RefreshDistance = false;
	};

	[[nodiscard]] constexpr GIFieldUpdateDecays SelectGIFieldUpdateDecays(
		const GIFieldReadinessSettings& settings, GIFieldRefreshReason reason, bool refreshing,
		bool entryUpdate, float steadyHysteresis) noexcept
	{
		// The entry update reweights at the refresh hysteresis under either integration.
		const bool rapid = refreshing && (entryUpdate ||
			GetGIFieldRefreshIntegration(settings, reason) == GIFieldRefreshIntegration::Rapid);
		const bool distance = refreshing && reason == GIFieldRefreshReason::HardReset;
		return { .m_Irradiance = settings.m_Enabled && rapid ? settings.m_RefreshHysteresis : steadyHysteresis,
			.m_Distance = settings.m_Enabled && distance ? settings.m_RefreshHysteresis : steadyHysteresis,
			.m_RefreshDistance = distance };
	}

	static_assert(GIFieldReadinessTraceUpdates == gi_shader::GIFieldReadinessTimeoutUpdates);

	class GIFieldReadinessEstimator final
	{
	public:
		// A trace observes this epoch's decisions only; it never changes them.
		void Reset(std::array<uint32_t, 3> counts, GIFieldReadinessSettings settings,
			GIFieldRefreshReason reason = GIFieldRefreshReason::HardReset, bool trace = false) noexcept
		{
			m_Counts = counts;
			m_Settings = settings;
			m_Reason = reason;
			m_Trace = trace;
			m_TraceTail.reset();
			const float sigma = GetGIFieldReadinessCalibration(settings, reason).m_RefreshSigma;
			m_Snapshot = { .m_State = reason == GIFieldRefreshReason::Lighting ?
					GIFieldReadinessState::Settling :
					GIFieldReadinessState::Initializing,
				.m_Enabled = settings.m_Enabled, .m_Refreshing = settings.m_Enabled, .m_NoiseSigma = sigma,
				.m_Threshold = std::max(gi_shader::GIFieldReadinessMinimumThreshold, 3 * sigma),
				.m_Hysteresis = settings.m_RefreshHysteresis };
			const uint32_t width = gi_shader::GIFieldReadinessBlockWidth;
			const uint32_t blocks = ((counts[0] + width - 1) / width) *
				((counts[1] + width - 1) / width) * ((counts[2] + width - 1) / width);
			m_QuietUpdates.assign(blocks, 0);
			for (auto& sums : m_NoiseSquares)
			{
				sums.assign(uint64_t(counts[0]) * counts[1] * counts[2], 0);
			}
			m_NoiseSamples = {};
			m_ModeUpdates = 0;
			m_HasMode = false;
			m_Measurement.reset();
		}
		[[nodiscard]] const GIFieldReadinessSnapshot& GetSnapshot() const noexcept { return m_Snapshot; }
		[[nodiscard]] std::shared_ptr<const GIFieldReadinessMeasurement> GetMeasurement() const noexcept { return m_Measurement; }
		bool Observe(uint64_t update, float floor, std::span<const GIFieldProbeChange> probes, float hysteresis = 0.5f, bool refresh = true) noexcept
		{
			if (!m_Settings.m_Enabled || update <= m_Snapshot.m_CompletedUpdates)
			{
				return false;
			}
			if (update != m_Snapshot.m_CompletedUpdates + 1 || !std::isfinite(floor) || floor < gi_shader::GIFieldReadinessAbsoluteFloor ||
				probes.size() != uint64_t(m_Counts[0]) * m_Counts[1] * m_Counts[2])
			{
				m_Snapshot.m_Failed = true;
				return false;
			}
			if (m_HasMode && refresh != m_RefreshMode)
			{
				std::fill(m_QuietUpdates.begin(), m_QuietUpdates.end(), 0);
				m_ModeUpdates = 0;
			}
			m_HasMode = true;
			m_RefreshMode = refresh;
			++m_ModeUpdates;
			m_Snapshot.m_Hysteresis = hysteresis;
			const auto& calibration = GetGIFieldReadinessCalibration(m_Settings, m_Reason);
			m_Snapshot.m_NoiseSigma = refresh ? calibration.m_RefreshSigma : calibration.m_SteadySigma;
			m_Snapshot.m_Threshold = std::max(gi_shader::GIFieldReadinessMinimumThreshold, 3 * m_Snapshot.m_NoiseSigma);
			m_Snapshot.m_CompletedUpdates = update;
			m_Snapshot.m_IrradianceFloor = floor;
			m_Snapshot.m_ActiveBlocks = 0;
			m_Snapshot.m_ExcludedBlocks = 0;
			m_Snapshot.m_QuietBlocks = 0;
			m_Snapshot.m_MaximumChange = 0;
			const bool lighting = m_Reason == GIFieldRefreshReason::Lighting;
			const uint32_t warming = lighting ? gi_shader::GIFieldReadinessChangeSpan :
				std::max(gi_shader::GIFieldReadinessMinimumWarmUpdates,
					uint32_t(std::ceil(1.0 / (1.0 - m_Settings.m_RefreshHysteresis))));
			const uint32_t width = gi_shader::GIFieldReadinessBlockWidth;
			auto measurement = std::make_shared<GIFieldReadinessMeasurement>();
			measurement->m_Counts = m_Counts;
			measurement->m_BlockWidth = width;
			measurement->m_WarmingUpdates = warming;
			measurement->m_RequiredQuietUpdates = gi_shader::GIFieldReadinessQuietUpdates;
			measurement->m_RefreshMode = refresh;
			measurement->m_IncludesDistance = !lighting;
			measurement->m_Probes.resize(probes.size());
			measurement->m_Blocks.resize(m_QuietUpdates.size());
			uint32_t block = 0;
			for (uint32_t y = 0; y < m_Counts[1]; y += width)
			{
				for (uint32_t z = 0; z < m_Counts[2]; z += width)
				{
					for (uint32_t x = 0; x < m_Counts[0]; x += width, ++block)
					{
						std::array<float, 64> changes{};
						auto& explanation = measurement->m_Blocks[block];
						size_t active = 0;
						for (uint32_t py = y; py < std::min(y + width, m_Counts[1]); ++py)
						{
							for (uint32_t pz = z; pz < std::min(z + width, m_Counts[2]); ++pz)
							{
								for (uint32_t px = x; px < std::min(x + width, m_Counts[0]); ++px)
								{
									const auto& probe = probes[px + m_Counts[0] * (pz + m_Counts[2] * py)];
									if (!std::isfinite(probe.m_Change) || probe.m_Change < 0 ||
										!std::isfinite(probe.m_Luminance) || probe.m_Luminance < 0 ||
										(probe.m_Active != 0 && probe.m_Active != 1) || probe.m_Updates != update)
									{
										m_Snapshot.m_Failed = true;
										return false;
									}
									if (probe.m_Active != 0)
									{
										changes[active++] = probe.m_Change;
									}
									measurement->m_Probes[px + m_Counts[0] * (pz + m_Counts[2] * py)] =
										{ probe.m_Change, probe.m_Active != 0 };
								}
							}
						}
						if (active == 0)
						{
							++m_Snapshot.m_ExcludedBlocks;
							continue;
						}
						++m_Snapshot.m_ActiveBlocks;
						std::sort(changes.begin(), changes.begin() + active);
						const float maximum = changes[active - 1];
						m_Snapshot.m_MaximumChange = std::max(m_Snapshot.m_MaximumChange, maximum);
						const size_t percentile = (95 * active + 99) / 100 - 1;
						explanation.m_ActiveProbes = uint32_t(active);
						explanation.m_Percentile95 = changes[percentile];
						explanation.m_Maximum = maximum;
						explanation.m_WarmingMet = update >= warming;
						const bool thresholdQuiet = changes[percentile] <= m_Snapshot.m_Threshold &&
							maximum <= 3 * m_Snapshot.m_Threshold;
						const bool quiet = update >= warming && thresholdQuiet;
						m_QuietUpdates[block] = quiet ? std::min(m_QuietUpdates[block] + 1, gi_shader::GIFieldReadinessQuietUpdates) : 0;
						m_Snapshot.m_QuietBlocks += m_QuietUpdates[block] >= gi_shader::GIFieldReadinessQuietUpdates ? 1 : 0;
						explanation.m_CurrentQuiet = thresholdQuiet;
						explanation.m_QuietUpdates = m_QuietUpdates[block];
						explanation.m_Blockers =
							(update < warming ? uint32_t(GIFieldReadinessBlocker::Warming) : 0u) |
							(changes[percentile] > m_Snapshot.m_Threshold ? uint32_t(GIFieldReadinessBlocker::Percentile) : 0u) |
							(maximum > 3 * m_Snapshot.m_Threshold ? uint32_t(GIFieldReadinessBlocker::Maximum) : 0u) |
							(m_QuietUpdates[block] < gi_shader::GIFieldReadinessQuietUpdates ? uint32_t(GIFieldReadinessBlocker::QuietHold) : 0u);
					}
				}
			}
			// Report a bounded frozen-input noise experiment; never feed its result back
			// into this owner's thresholds. A separate profile must explicitly adopt it.
			const size_t mode = refresh ? 0 : 1;
			if (m_ModeUpdates >= gi_shader::GIFieldReadinessMinimumWarmUpdates && m_NoiseSamples[mode] < gi_shader::GIFieldReadinessNoiseSamples)
			{
				++m_NoiseSamples[mode];
				double maximum = 0;
				for (size_t index = 0; index < probes.size(); ++index)
				{
					const auto& probe = probes[index];
					if (probe.m_Active != 0)
					{
						m_NoiseSquares[mode][index] += double(probe.m_Change) * probe.m_Change;
						maximum = std::max(maximum, m_NoiseSquares[mode][index]);
					}
				}
				const float estimate = static_cast<float>(std::sqrt(maximum / m_NoiseSamples[mode]));
				if (refresh)
				{
					m_Snapshot.m_RefreshNoiseEstimate = estimate;
					m_Snapshot.m_RefreshNoiseSamples = m_NoiseSamples[mode];
				}
				else
				{
					m_Snapshot.m_SteadyNoiseEstimate = estimate;
					m_Snapshot.m_SteadyNoiseSamples = m_NoiseSamples[mode];
				}
			}
			const bool ready = (m_Snapshot.m_ActiveBlocks == 0 || update >= warming) &&
				m_Snapshot.m_QuietBlocks == m_Snapshot.m_ActiveBlocks;
			if (ready)
			{
				if (m_Snapshot.m_State != GIFieldReadinessState::Ready)
				{
					m_Snapshot.m_ReadyUpdate = update;
				}
				m_Snapshot.m_State = GIFieldReadinessState::Ready;
				m_Snapshot.m_Refreshing = false;
			}
			else
			{
				m_Snapshot.m_ReadyUpdate = 0;
				m_Snapshot.m_State = update >= gi_shader::GIFieldReadinessTimeoutUpdates ? GIFieldReadinessState::Unconverged :
					update < warming && !lighting ? GIFieldReadinessState::Warming : GIFieldReadinessState::Settling;
				m_Snapshot.m_Refreshing = m_Snapshot.m_Refreshing && update < gi_shader::GIFieldReadinessTimeoutUpdates;
			}
			measurement->m_Readiness = m_Snapshot;
			if (m_Trace && update <= GIFieldReadinessTraceUpdates)
			{
				GIFieldReadinessTraceUpdate record{ .m_Readiness = m_Snapshot, .m_RefreshMode = refresh,
					.m_Blocks = measurement->m_Blocks, .m_Previous = std::move(m_TraceTail) };
				m_TraceTail = std::make_shared<const GIFieldReadinessTraceUpdate>(std::move(record));
			}
			measurement->m_Trace = m_TraceTail;
			m_Measurement = std::move(measurement);
			return true;
		}

	private:
		std::array<uint32_t, 3> m_Counts{};
		GIFieldReadinessSettings m_Settings;
		GIFieldReadinessSnapshot m_Snapshot;
		std::shared_ptr<const GIFieldReadinessMeasurement> m_Measurement;
		std::shared_ptr<const GIFieldReadinessTraceUpdate> m_TraceTail;
		std::vector<uint32_t> m_QuietUpdates;
		std::array<std::vector<double>, 2> m_NoiseSquares;
		std::array<uint32_t, 2> m_NoiseSamples{};
		uint32_t m_ModeUpdates = 0;
		bool m_HasMode = false;
		bool m_RefreshMode = true;
		GIFieldRefreshReason m_Reason = GIFieldRefreshReason::HardReset;
		bool m_Trace = false;
	};

	// The next update takes the refresh decision observed through the last submitted one.
	// Ready and the timeout only end a refresh, so a closed refresh needs no further
	// observation, while an open one must first observe every submitted update of its
	// epoch. The steady policy then starts exactly one update after the update that ended
	// the refresh, however early or late each readback completes.
	[[nodiscard]] constexpr bool RequiresGIFieldRefreshPrefix(const GIFieldReadinessSnapshot& readiness,
		uint64_t epochOrigin, uint64_t submittedUpdates) noexcept
	{
		return readiness.m_Enabled && readiness.m_Refreshing && !readiness.m_Failed &&
			submittedUpdates > epochOrigin + readiness.m_CompletedUpdates;
	}

	[[nodiscard]] inline bool IsGIUpdateStatusValid(const GIUpdateStatus& status,
		uint32_t probes, uint32_t rays, uint64_t submittedUpdates,
		uint32_t irradianceInteriorWidth = gi_shader::GIProbeIrradianceInteriorWidth) noexcept
	{
		if (status.m_Error == GIUpdateError::None)
		{
			return status.m_Probe == UINT32_MAX && status.m_Update == UINT32_MAX && status.m_Element == UINT32_MAX;
		}
		const bool known = status.m_Error == GIUpdateError::InvalidRay ||
			status.m_Error == GIUpdateError::InvalidHistory || status.m_Error == GIUpdateError::InvalidBlend;
		const uint32_t interiorTexels = irradianceInteriorWidth * irradianceInteriorWidth +
			gi_shader::GIProbeDistanceInteriorTexels;
		return known && status.m_Probe < probes && status.m_Update < submittedUpdates &&
			status.m_Element < (status.m_Error == GIUpdateError::InvalidRay ? rays : interiorTexels);
	}

	// CPU state only: neither deferred commits nor Renderer settlement own GPU objects.
	// In-place GPU writes cannot be rolled back after a failed/unknown submission.
	class GIFieldUpdateState final
	{
	public:
		bool Begin(uint64_t serial) noexcept
		{
			if (m_Invalid || serial == 0 || serial <= m_Serial || (m_Serial != 0 && !m_Settled))
			{
				return false;
			}
			m_Serial = serial;
			m_Recorded = false;
			m_Settled = false;
			m_Committed = false;
			return true;
		}
		void MarkRecorded(uint64_t serial) noexcept
		{
			if (serial == m_Serial && !m_Settled)
			{
				m_Recorded = true;
			}
		}
		void Settle(const RenderFrameOutcome& outcome) noexcept
		{
			if (outcome.m_FrameSerial != m_Serial || m_Settled)
			{
				return;
			}
			m_Settled = true;
			m_Submitted = outcome.m_Kind == RenderFrameOutcomeKind::Submitted && outcome.m_Fence.IsValid();
			m_Invalid = outcome.m_Kind == RenderFrameOutcomeKind::Failed ||
				outcome.m_Kind == RenderFrameOutcomeKind::CompletionUnknown ||
				(outcome.m_Kind == RenderFrameOutcomeKind::Submitted && !outcome.m_Fence.IsValid());
		}
		bool Commit(uint64_t serial) noexcept
		{
			if (serial != m_Serial || !m_Settled || !m_Submitted || !m_Recorded || m_Committed || m_Invalid)
			{
				return false;
			}
			m_Committed = true;
			return true;
		}
		[[nodiscard]] bool IsInvalid() const noexcept { return m_Invalid; }

	private:
		uint64_t m_Serial = 0;
		bool m_Recorded = false;
		bool m_Settled = false;
		bool m_Submitted = false;
		bool m_Committed = false;
		bool m_Invalid = false;
	};

	// A renderer frame has at most one field. This independent CPU lease survives a
	// safe pipeline replacement without prolonging its scene, assets or native resources.
	class GIFieldFrameState final
	{
	public:
		void BeginFrame(uint64_t serial) noexcept
		{
			m_Serial = serial;
			m_Field.reset();
		}
		bool Track(const std::shared_ptr<GIFieldUpdateState>& field, uint64_t serial) noexcept
		{
			if (!field || serial != m_Serial || m_Field || !field->Begin(serial))
			{
				return false;
			}
			m_Field = field;
			return true;
		}
		void Settle(const RenderFrameOutcome& outcome) noexcept
		{
			if (m_Field && outcome.m_FrameSerial == m_Serial)
			{
				m_Field->Settle(outcome);
				m_Field.reset();
			}
		}

	private:
		uint64_t m_Serial = 0;
		std::shared_ptr<GIFieldUpdateState> m_Field;
	};
}
