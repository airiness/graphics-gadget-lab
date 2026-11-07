#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIFieldReadiness.h"
#include "GGLabRuntime/Graphics/Pipeline/GIFieldProfile.h"
#include "GGLabRuntime/Graphics/Pipeline/GIProbeVisualization.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace gglab
{
	// Last fence-completed full-volume feedback update. Counts include all traced
	// surface hits; every consumed probe belongs to the previous submitted update.
	struct GIFieldFeedbackSnapshot
	{
		bool m_Enabled = false;
		uint64_t m_CompletedUpdate = 0;
		uint64_t m_SourceUpdate = 0;
		uint64_t m_Valid = 0;
		uint64_t m_LowConfidence = 0;
		uint64_t m_NoValidSupport = 0;
		uint64_t m_OutsideCoverage = 0;
		uint64_t m_Initializing = 0;
		uint64_t m_Incompatible = 0;
	};

	struct GIFieldIdentity
	{
		uint64_t m_Generation = 0;
		uint64_t m_SceneRevision = 0;
		uint64_t m_LightingGeneration = 0;
		uint64_t m_EnvironmentGeneration = 0;
		uint64_t m_RefreshEpoch = 0;
		uint64_t m_EpochOriginUpdate = 0;
		bool operator==(const GIFieldIdentity&) const = default;
	};

	enum class GIObservationKind : uint8_t
	{
		CompletedReadiness,
		ProbeAtlas,
		ReceiverContribution,
		ProbeVisualization,
		ProbePicking,
	};

	struct GIObservationStamp
	{
		GIFieldIdentity m_Field;
		GIObservationKind m_Kind = GIObservationKind::CompletedReadiness;
		uint64_t m_ProducingFrame = 0;
		uint64_t m_FieldUpdate = 0;
		uint64_t m_ReadinessUpdate = 0;
	};

	struct GIFieldChangeObservation
	{
		GIObservationStamp m_Observation;
		std::shared_ptr<const GIFieldReadinessMeasurement> m_Measurement;
	};

	struct GIReceiverViewSnapshot
	{
		uint64_t m_Revision = 0;
		uint32_t m_ViewId = 0;
		std::array<uint32_t, 2> m_RenderExtent{};
		std::array<uint32_t, 2> m_DisplayExtent{};
		std::array<float, 3> m_CameraPosition{};
		// Row-major matrices for row-vector multiplication; clip depth is [0,1].
		std::array<float, 16> m_View{};
		std::array<float, 16> m_Projection{};
		std::array<float, 16> m_ViewProjection{};
		std::array<float, 2> m_JitterPixels{};
		std::array<float, 2> m_NearFar{};
		uint64_t m_ReceiverAttachmentBytes = 0;
		uint64_t m_LookupDiagnosticBytes = 0;
		bool m_ReversedDepth = false;
		bool m_ReceiversAvailable = false;
		bool m_TemporalAA = false;
		bool m_Supported = false;
		bool operator==(const GIReceiverViewSnapshot&) const = default;
	};

	struct GIProbeVisualizationViewSnapshot
	{
		uint64_t m_Revision = 0;
		uint32_t m_ViewId = 0;
		std::array<uint32_t, 2> m_RenderExtent{};
		std::array<uint32_t, 2> m_DisplayExtent{};
		std::array<float, 3> m_CameraPosition{};
		// Row-major matrices for row-vector multiplication; clip depth is [0,1].
		std::array<float, 16> m_View{};
		std::array<float, 16> m_UnjitteredProjection{};
		std::array<float, 16> m_UnjitteredViewProjection{};
		// The actual post-temporal projection for this execution, before viewport mapping.
		std::array<float, 16> m_Projection{};
		std::array<float, 16> m_ViewProjection{};
		std::array<float, 2> m_JitterPixels{};
		std::array<float, 2> m_NearFar{};
		uint32_t m_TemporalFrameIndex = 0;
		TemporalAAResolutionPreset m_ResolutionPreset = TemporalAAResolutionPreset::Native;
		bool m_ReversedDepth = false;
		bool m_TemporalAA = false;
		bool m_PostTemporalJittered = false;
		bool m_ProjectionSupported = false;
		bool m_Valid = false;
		bool operator==(const GIProbeVisualizationViewSnapshot&) const = default;
	};

	[[nodiscard]] inline std::string_view GetGIProbeVisualizationDepthSourceName(
		const GIProbeVisualizationViewSnapshot& view) noexcept
	{
		return view.m_RenderExtent == view.m_DisplayExtent ?
			"DisplayView.DepthBuffer" : "DisplayView.DisplayDepth";
	}

	[[nodiscard]] inline std::string_view GetGIProbeVisualizationDepthAlignmentName(
		const GIProbeVisualizationViewSnapshot& view) noexcept
	{
		if (view.m_RenderExtent != view.m_DisplayExtent)
		{
			return "upscaled-nearest-sample-approximation";
		}
		if (!view.m_TemporalAA)
		{
			return "native-exact";
		}
		return view.m_PostTemporalJittered ?
			"native-jittered-aligned" : "native-jittered-depth-approximation";
	}

	[[nodiscard]] inline bool IsGIProbeVisualizationViewSupported(
		const GIProbeVisualizationViewSnapshot& view) noexcept
	{
		const auto expected = ResolveTemporalAAViewResolution(
			{ view.m_DisplayExtent[0], view.m_DisplayExtent[1] }, view.m_ResolutionPreset);
		return view.m_Revision != 0 && view.m_Valid && view.m_ProjectionSupported &&
			view.m_DisplayExtent[0] != 0 && view.m_DisplayExtent[1] != 0 &&
			(view.m_ResolutionPreset == TemporalAAResolutionPreset::Native ||
				view.m_ResolutionPreset == TemporalAAResolutionPreset::Quality) &&
			(!view.m_PostTemporalJittered || (view.m_TemporalAA &&
				view.m_ResolutionPreset == TemporalAAResolutionPreset::Native)) &&
			view.m_RenderExtent == std::array{ expected.m_Render.m_Width, expected.m_Render.m_Height } &&
			(view.m_TemporalAA || (view.m_ResolutionPreset == TemporalAAResolutionPreset::Native &&
				view.m_JitterPixels == std::array<float, 2>{}));
	}

	[[nodiscard]] inline GIProbeVisualizationViewSnapshot AdvanceGIProbeVisualizationView(
		const GIProbeVisualizationViewSnapshot& previous, GIProbeVisualizationViewSnapshot next) noexcept
	{
		// Execution phase and raster matrices change with jitter; logical camera,
		// projection policy and resolution changes invalidate a future selection request.
		auto logical = [](GIProbeVisualizationViewSnapshot value)
			{
				value.m_Revision = 0;
				value.m_TemporalFrameIndex = 0;
				value.m_JitterPixels = {};
				value.m_Projection = value.m_UnjitteredProjection;
				value.m_ViewProjection = value.m_UnjitteredViewProjection;
				return value;
			};
		next.m_Revision = previous.m_Revision + (logical(previous) != logical(next) ? 1 : 0);
		return next;
	}

	// The revision follows logical camera/configuration changes, not frame advancement.
	// The revision follows logical camera/configuration changes, not frame advancement.
	[[nodiscard]] inline GIReceiverViewSnapshot AdvanceGIReceiverView(
		const GIReceiverViewSnapshot& previous, GIReceiverViewSnapshot next) noexcept
	{
		next.m_Revision = previous.m_Revision;
		if (next != previous)
		{
			++next.m_Revision;
		}
		return next;
	}

	enum class GIProbeHighlightState : uint8_t
	{
		None,
		Disabled,
		Unavailable,
		FieldMismatch,
		OutOfRange,
		Ready,
	};

	[[nodiscard]] inline std::string_view GetGIProbeHighlightStateName(GIProbeHighlightState state) noexcept
	{
		constexpr std::array<std::string_view, 6> names
		{
			"none", "disabled", "unavailable", "field-mismatch", "out-of-range", "ready"
		};
		const auto index = static_cast<size_t>(state);
		return index < names.size() ? names[index] : "unknown";
	}

	[[nodiscard]] inline GIProbeHighlightState ResolveGIProbeHighlightState(
		const GIProbeVisualizationSettings& settings, uint64_t generation,
		const std::array<uint32_t, 3>& counts, bool available, bool highlightAvailable) noexcept
	{
		if (!settings.m_Selection)
		{
			return GIProbeHighlightState::None;
		}
		const auto& selection = *settings.m_Selection;
		if (selection.m_ExpectedGeneration == 0 || selection.m_ExpectedGeneration != generation)
		{
			return GIProbeHighlightState::FieldMismatch;
		}
		for (size_t axis = 0; axis < counts.size(); ++axis)
		{
			if (selection.m_Coordinate[axis] >= counts[axis])
			{
				return GIProbeHighlightState::OutOfRange;
			}
		}
		return !settings.m_Enabled ?
			GIProbeHighlightState::Disabled :
			available && highlightAvailable ? GIProbeHighlightState::Ready : GIProbeHighlightState::Unavailable;
	}

	// Published together on the Runtime owner thread. Copies retain CPU values only.
	struct GIProbeVisualizationSnapshot
	{
		uint64_t m_SettingsRevision = 0;
		bool m_PickingAvailable = false;
		std::shared_ptr<const GIFieldChangeObservation> m_ChangeObservation;
		uint64_t m_ChangeUploadBytes = 0;
		GIProbeVisualizationSettings m_Settings;
		bool m_Available = false;
		bool m_ViewSupported = false;
		bool m_ReadbackFailed = false;
		bool m_HighlightAvailable = false;
		uint32_t m_HighlightCount = 0;
		GIProbeHighlightState m_HighlightState = GIProbeHighlightState::None;
		uint64_t m_PrivateDepthLogicalBytes = 0;
		uint64_t m_SelectionLogicalBytes = 0;
		uint64_t m_ReadbackBufferBytes = 0;
		// Retained native ring allocation, including alignment; unavailable is not zero.
		std::optional<uint64_t> m_ReadbackAllocatedBytes;
		std::optional<GIObservationStamp> m_Observation;
		std::optional<GIObservationStamp> m_CompletedObservation;
		GIProbeVisualizationViewSnapshot m_View;
		GIProbeVisualizationViewSnapshot m_CompletedView;
		GIProbeVisualizationSettings m_CompletedSettings;
		GIProbeVisualizationCounts m_CompletedCounts;
	};

	struct GIFieldSummary
	{
		std::shared_ptr<const GIFieldChangeObservation> m_ChangeObservation;
		GIFieldIdentity m_Field;
		std::shared_ptr<const GIFieldProfileReceipt> m_Profile;
		std::array<uint32_t, 3> m_Counts{};
		std::array<float, 3> m_Minimum{};
		float m_Spacing = 0;
		uint32_t m_IrradianceInteriorWidth = 0;
		uint64_t m_SubmittedUpdates = 0;
		// Diagnostic hold, zero when unlimited. A held frame records no update: its
		// consumers observe update m_SubmittedUpdates instead of the next one.
		uint32_t m_UpdateLimit = 0;
		bool m_UpdateHeld = false;
		GIFieldReadinessSnapshot m_Readiness;
		std::optional<GIObservationStamp> m_CompletedMeasurement;
		bool m_Initialized = false;
		bool m_Invalid = false;
		bool m_InspectionAvailable = false;
		bool m_ReceiverInspectionAvailable = false;
		bool m_ReceiverMarkersAvailable = false;
		GIReceiverViewSnapshot m_ReceiverView;
		GIProbeVisualizationSnapshot m_Visualization;
	};

	// One-based update whose contents this frame's GPU consumers observe: the update
	// the frame records, or the last recorded one while updates are held or invalid.
	[[nodiscard]] inline uint64_t GetGIFieldFrameUpdate(const GIFieldSummary& summary) noexcept
	{
		return summary.m_SubmittedUpdates + (summary.m_UpdateHeld || summary.m_Invalid ? 0 : 1);
	}

	[[nodiscard]] inline bool MatchesGIProbeSelection(const GIProbeSelection& selection,
		uint64_t generation, const std::array<uint32_t, 3>& counts) noexcept
	{
		return selection.m_ExpectedGeneration != 0 && selection.m_ExpectedGeneration == generation &&
			selection.m_Coordinate[0] < counts[0] && selection.m_Coordinate[1] < counts[1] &&
			selection.m_Coordinate[2] < counts[2];
	}

	// A completed execution can match the current logical view at a different jitter phase.
	[[nodiscard]] inline bool MatchesGIProbeVisualizationCounts(const GIFieldSummary& summary) noexcept
	{
		const auto& display = summary.m_Visualization;
		return display.m_CompletedObservation && display.m_CompletedObservation->m_Field == summary.m_Field &&
			display.m_CompletedSettings == display.m_Settings &&
			display.m_CompletedView.m_Revision == display.m_View.m_Revision &&
			display.m_CompletedCounts.m_Highlights == display.m_HighlightCount;
	}

	struct GIProbeInspectionRequest
	{
		std::array<uint32_t, 3> m_Coordinate{};
		uint64_t m_ExpectedGeneration = 0;
		// Omit to select the current epoch at issue; generation is always required.
		std::optional<uint64_t> m_ExpectedRefreshEpoch;
		// Presence requests the versioned raw integration witness. Zero uses the current
		// update; a positive one-based update replays directions only, never field history.
		std::optional<uint32_t> m_IntegrationReplayUpdate;
		bool operator==(const GIProbeInspectionRequest&) const = default;
	};

	enum class GIProbeInspectionValidity : uint8_t
	{
		Valid,
		Inactive,
		InvalidField,
		InvalidNumerics,
		MalformedPayload,
	};

	[[nodiscard]] inline std::string_view GetGIProbeInspectionValidityName(GIProbeInspectionValidity value) noexcept
	{
		constexpr std::array<std::string_view, 5> names{
			"Valid", "Inactive", "InvalidField", "InvalidNumerics", "MalformedPayload" };
		const auto index = static_cast<size_t>(value);
		return index < names.size() ? names[index] : "Unknown";
	}

	struct GIProbeInspectionResult
	{
		GIProbeInspectionRequest m_Request;
		GIObservationStamp m_Observation;
		std::array<float, 3> m_WorldPosition{};
		uint32_t m_Classification = 0;
		uint32_t m_IrradianceTileWidth = 0;
		uint32_t m_DistanceTileWidth = 16;
		GIProbeInspectionValidity m_Validity = GIProbeInspectionValidity::MalformedPayload;
		// Row-major bordered octahedral tiles, with y increasing as atlas texel y.
		// Irradiance E is scene-referred, before albedo, exposure or tone mapping.
		std::vector<std::array<float, 4>> m_Irradiance;
		// Mean distance in world units and second moment in squared world units.
		std::vector<std::array<float, 4>> m_Distance;
	};

	[[nodiscard]] inline bool MatchesGIProbeInspection(const GIProbeInspectionRequest& request,
		const GIFieldSummary& summary) noexcept
	{
		return request.m_ExpectedGeneration != 0 &&
			request.m_ExpectedGeneration == summary.m_Field.m_Generation &&
			(!request.m_ExpectedRefreshEpoch ||
				*request.m_ExpectedRefreshEpoch == summary.m_Field.m_RefreshEpoch) &&
			request.m_Coordinate[0] < summary.m_Counts[0] &&
			request.m_Coordinate[1] < summary.m_Counts[1] &&
			request.m_Coordinate[2] < summary.m_Counts[2];
	}

	inline constexpr uint32_t GIProbeInspectionMagic = 0x47504931;
	inline constexpr uint32_t GIProbeInspectionMaximumBytes = (1 + 32 * 32 + 16 * 16) * 16;

	[[nodiscard]] inline std::shared_ptr<const GIProbeInspectionResult> DecodeGIProbeInspection(
		GIProbeInspectionRequest request, GIObservationStamp observation,
		std::array<float, 3> position, std::span<const uint8_t> bytes) noexcept
	{
		auto result = std::make_shared<GIProbeInspectionResult>();
		result->m_Request = request;
		result->m_Observation = observation;
		result->m_WorldPosition = position;
		std::array<uint32_t, 4> header{};
		if (bytes.size() < sizeof(header) || bytes.size() > GIProbeInspectionMaximumBytes ||
			request.m_ExpectedGeneration == 0 || request.m_ExpectedGeneration != observation.m_Field.m_Generation ||
			(request.m_ExpectedRefreshEpoch && *request.m_ExpectedRefreshEpoch != observation.m_Field.m_RefreshEpoch) ||
			observation.m_Kind != GIObservationKind::ProbeAtlas || observation.m_ProducingFrame == 0 ||
			observation.m_FieldUpdate <= observation.m_Field.m_EpochOriginUpdate ||
			observation.m_ReadinessUpdate != observation.m_FieldUpdate - observation.m_Field.m_EpochOriginUpdate)
		{
			return result;
		}
		std::memcpy(header.data(), bytes.data(), sizeof(header));
		const uint32_t width = header[3] + 2;
		if (header[0] != GIProbeInspectionMagic || (width != 8 && width != 16 && width != 32) ||
			bytes.size() != uint64_t(1 + width * width + 16 * 16) * 16 || (header[1] != 1 && header[1] != 2))
		{
			return result;
		}
		result->m_Classification = header[1];
		result->m_IrradianceTileWidth = width;
		result->m_Irradiance.resize(width * width);
		result->m_Distance.resize(16 * 16);
		const size_t irradianceBytes = result->m_Irradiance.size() * 16;
		std::memcpy(result->m_Irradiance.data(), bytes.data() + 16, irradianceBytes);
		std::memcpy(result->m_Distance.data(), bytes.data() + 16 + irradianceBytes, 16 * 16 * 16);
		result->m_Validity = header[2] != 0 ? GIProbeInspectionValidity::InvalidField :
			header[1] == 1 ? GIProbeInspectionValidity::Valid : GIProbeInspectionValidity::Inactive;
		for (const auto* tile : { &result->m_Irradiance, &result->m_Distance })
		{
			for (const auto& texel : *tile)
			{
				for (float value : texel)
				{
					if (!std::isfinite(value))
					{
						result->m_Validity = GIProbeInspectionValidity::InvalidNumerics;
					}
				}
			}
		}
		return result;
	}
}
