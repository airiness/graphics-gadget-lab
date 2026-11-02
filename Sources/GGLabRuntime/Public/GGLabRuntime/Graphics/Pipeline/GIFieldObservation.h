#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIFieldReadiness.h"

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
	};

	struct GIObservationStamp
	{
		GIFieldIdentity m_Field;
		GIObservationKind m_Kind = GIObservationKind::CompletedReadiness;
		uint64_t m_ProducingFrame = 0;
		uint64_t m_FieldUpdate = 0;
		uint64_t m_ReadinessUpdate = 0;
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

	// Published together on the Runtime owner thread. Copies retain CPU values only.
	struct GIFieldSummary
	{
		GIFieldIdentity m_Field;
		std::array<uint32_t, 3> m_Counts{};
		std::array<float, 3> m_Minimum{};
		float m_Spacing = 0;
		uint32_t m_IrradianceInteriorWidth = 0;
		uint64_t m_SubmittedUpdates = 0;
		GIFieldReadinessSnapshot m_Readiness;
		std::optional<GIObservationStamp> m_CompletedMeasurement;
		bool m_Initialized = false;
		bool m_Invalid = false;
		bool m_InspectionAvailable = false;
		bool m_ReceiverInspectionAvailable = false;
		bool m_ReceiverMarkersAvailable = false;
		GIReceiverViewSnapshot m_ReceiverView;
	};

	struct GIProbeInspectionRequest
	{
		std::array<uint32_t, 3> m_Coordinate{};
		uint64_t m_ExpectedGeneration = 0;
		// Omit to select the current epoch at issue; generation is always required.
		std::optional<uint64_t> m_ExpectedRefreshEpoch;
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
