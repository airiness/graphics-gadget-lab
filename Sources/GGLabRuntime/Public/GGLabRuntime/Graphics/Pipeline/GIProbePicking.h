#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIFieldObservation.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace gglab
{
	struct GIProbePickMapping
	{
		std::array<float, 2> m_GuiOrigin{};
		std::array<float, 2> m_GuiExtent{};
		std::array<float, 2> m_FramebufferScale{ 1, 1 };
		float m_DpiScale = 1;
		bool operator==(const GIProbePickMapping&) const = default;
	};

	[[nodiscard]] inline std::optional<std::array<uint32_t, 2>> MapGIProbePickPixel(
		const GIProbePickMapping& mapping, const std::array<uint32_t, 2>& display,
		const std::array<float, 2>& point) noexcept
	{
		if (!std::isfinite(mapping.m_DpiScale) || mapping.m_DpiScale <= 0)
		{
			return {};
		}
		std::array<uint32_t, 2> pixel{};
		for (size_t axis = 0; axis < 2; ++axis)
		{
			const float scale = mapping.m_FramebufferScale[axis];
			const float extent = mapping.m_GuiExtent[axis];
			const float origin = mapping.m_GuiOrigin[axis];
			const double coordinate = (double(point[axis]) - origin) * scale;
			if (!std::isfinite(origin) || !std::isfinite(extent) || !std::isfinite(scale) ||
				extent <= 0 || scale <= 0 || !std::isfinite(coordinate) || display[axis] == 0 ||
				std::abs(double(extent) * scale - display[axis]) > 0.01 ||
				coordinate < 0 || coordinate >= display[axis])
			{
				return {};
			}
			pixel[axis] = static_cast<uint32_t>(std::floor(coordinate));
		}
		return pixel;
	}

	[[nodiscard]] inline GIProbeVisualizationSettings GIProbePickSettings(GIProbeVisualizationSettings settings) noexcept
	{
		// Diagnostic highlights are not ordinary pick candidates.
		settings.m_Selection.reset();
		return settings;
	}

	struct GIProbePickingRequest
	{
		uint64_t m_Token = 0;
		uint64_t m_SourceFrame = 0;
		uint64_t m_SettingsRevision = 0;
		GIFieldIdentity m_Field;
		GIProbeVisualizationViewSnapshot m_View;
		GIProbeVisualizationSettings m_Settings;
		GIProbePickMapping m_Mapping;
		std::array<uint32_t, 2> m_Pixel{};
		bool operator==(const GIProbePickingRequest&) const = default;
	};

	enum class GIProbePickingBinding : uint8_t
	{
		Matching,
		InvalidSource,
		FieldMismatch,
		ViewMismatch,
		SettingsMismatch,
		MappingMismatch,
		Unavailable,
	};

	[[nodiscard]] inline std::string_view GetGIProbePickingBindingName(GIProbePickingBinding binding) noexcept
	{
		constexpr std::array<std::string_view, 7> names{
			"Matching", "InvalidSource", "FieldMismatch", "ViewMismatch", "SettingsMismatch", "MappingMismatch", "Unavailable" };
		const auto index = static_cast<size_t>(binding);
		return index < names.size() ? names[index] : "InvalidSource";
	}

	[[nodiscard]] inline bool IsGIProbePickingRequestValid(const GIProbePickingRequest& request) noexcept
	{
		if (!request.m_Token || !request.m_SourceFrame || !request.m_Field.m_Generation || !request.m_SettingsRevision ||
			!request.m_Settings.m_Enabled || !IsGIProbeVisualizationViewSupported(request.m_View) ||
			request.m_Pixel[0] >= request.m_View.m_DisplayExtent[0] || request.m_Pixel[1] >= request.m_View.m_DisplayExtent[1])
		{
			return false;
		}
		for (const auto* matrix : { &request.m_View.m_View, &request.m_View.m_UnjitteredProjection,
			&request.m_View.m_UnjitteredViewProjection })
		{
			for (const float value : *matrix)
			{
				if (!std::isfinite(value))
				{
					return false;
				}
			}
		}
		return MapGIProbePickPixel(request.m_Mapping, request.m_View.m_DisplayExtent,
			request.m_Mapping.m_GuiOrigin).has_value() &&
			GIProbePickSettings(request.m_Settings) == GIProbePickSettings(ResolveGIProbeVisualizationSettings(request.m_Settings));
	}

	[[nodiscard]] inline GIProbePickingBinding CheckGIProbePickingBinding(const GIProbePickingRequest& request,
		const GIFieldSummary& summary, const GIProbePickMapping& mapping) noexcept
	{
		if (!IsGIProbePickingRequestValid(request))
		{
			return GIProbePickingBinding::InvalidSource;
		}
		if (request.m_Field.m_Generation != summary.m_Field.m_Generation ||
			request.m_Field.m_SceneRevision != summary.m_Field.m_SceneRevision)
		{
			return GIProbePickingBinding::FieldMismatch;
		}
		const auto& display = summary.m_Visualization;
		if (request.m_View.m_Revision != display.m_View.m_Revision ||
			AdvanceGIProbeVisualizationView(request.m_View, display.m_View).m_Revision != request.m_View.m_Revision)
		{
			return GIProbePickingBinding::ViewMismatch;
		}
		if (request.m_SettingsRevision != display.m_SettingsRevision ||
			GIProbePickSettings(request.m_Settings) != GIProbePickSettings(display.m_Settings))
		{
			return GIProbePickingBinding::SettingsMismatch;
		}
		if (request.m_Mapping != mapping)
		{
			return GIProbePickingBinding::MappingMismatch;
		}
		return display.m_Available && display.m_PickingAvailable && !summary.m_Invalid ?
			GIProbePickingBinding::Matching : GIProbePickingBinding::Unavailable;
	}

	enum class GIProbePickingValidity : uint8_t
	{
		Hit,
		NoHit,
		MalformedPayload,
	};

	struct GIProbePickingResult
	{
		GIProbePickingRequest m_Request;
		GIObservationStamp m_Observation;
		GIProbeVisualizationViewSnapshot m_ExecutionView;
		GIProbePickingValidity m_Validity = GIProbePickingValidity::MalformedPayload;
		std::optional<uint32_t> m_ProbeId;
		std::optional<std::array<uint32_t, 3>> m_Coordinate;
		std::optional<float> m_Depth;
	};

	[[nodiscard]] inline std::shared_ptr<const GIProbePickingResult> DecodeGIProbePicking(
		const GIProbePickingRequest& request, const GIObservationStamp& observation,
		const GIProbeVisualizationViewSnapshot& execution, const std::array<uint32_t, 3>& counts,
		std::span<const std::byte> idBytes, std::span<const std::byte> depthBytes) noexcept
	{
		auto result = std::make_shared<GIProbePickingResult>();
		result->m_Request = request;
		result->m_Observation = observation;
		result->m_ExecutionView = execution;
		uint32_t encoded = 0;
		float depth = 0;
		const uint64_t row = uint64_t(counts[0]) * counts[2];
		if (idBytes.size() != 4 || depthBytes.size() != 4 || !IsGIProbePickingRequestValid(request) ||
			observation.m_Kind != GIObservationKind::ProbePicking ||
			observation.m_Field.m_Generation != request.m_Field.m_Generation ||
			observation.m_Field.m_SceneRevision != request.m_Field.m_SceneRevision ||
			observation.m_ProducingFrame <= request.m_SourceFrame || counts[0] == 0 || counts[1] == 0 || counts[2] == 0 ||
			row > UINT32_MAX || counts[1] > UINT32_MAX / row ||
			observation.m_FieldUpdate < observation.m_Field.m_EpochOriginUpdate ||
			observation.m_ReadinessUpdate != observation.m_FieldUpdate - observation.m_Field.m_EpochOriginUpdate ||
			!IsGIProbeVisualizationViewSupported(execution) || execution.m_Revision != request.m_View.m_Revision ||
			AdvanceGIProbeVisualizationView(request.m_View, execution).m_Revision != request.m_View.m_Revision)
		{
			return result;
		}
		std::memcpy(&encoded, idBytes.data(), 4);
		std::memcpy(&depth, depthBytes.data(), 4);
		if (!std::isfinite(depth) || depth < 0 || depth > 1 || uint64_t(encoded) > uint64_t(counts[0]) * counts[1] * counts[2])
		{
			return result;
		}
		if (encoded == 0)
		{
			if (depth == (execution.m_ReversedDepth ? 0.0f : 1.0f))
			{
				result->m_Validity = GIProbePickingValidity::NoHit;
			}
			return result;
		}
		if (depth == (execution.m_ReversedDepth ? 0.0f : 1.0f))
		{
			return result;
		}
		const uint32_t id = encoded - 1;
		result->m_Validity = GIProbePickingValidity::Hit;
		result->m_ProbeId = id;
		result->m_Coordinate = std::array<uint32_t, 3>{ id % counts[0], uint32_t(id / row), (id / counts[0]) % counts[2] };
		result->m_Depth = depth;
		return result;
	}
}
