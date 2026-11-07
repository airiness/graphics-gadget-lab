#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIFieldObservation.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace gglab
{
	struct GIReceiverInspectionRequest
	{
		// Integer display pixel, top-left origin; sample at its center.
		std::array<uint32_t, 2> m_Pixel{};
		uint64_t m_ExpectedGeneration = 0;
		uint64_t m_ExpectedViewRevision = 0;
		uint32_t m_ExpectedViewId = 0;
		std::optional<uint64_t> m_ExpectedRefreshEpoch;
		bool m_Markers = true;
		bool operator==(const GIReceiverInspectionRequest&) const = default;
	};

	enum class GIReceiverInspectionValidity : uint32_t
	{
		Valid,
		Background,
		FieldUnavailable,
		Incompatible,
		OutsideCoverage,
		NoValidSupport,
		InvalidNumerics,
		MalformedPayload,
	};

	enum class GIReceiverCornerReason : uint32_t
	{
		NotEvaluated,
		NotActive,
		ZeroTrilinearWeight,
		ZeroWeight,
		Contributing,
	};

	[[nodiscard]] inline std::string_view GetGIReceiverInspectionValidityName(GIReceiverInspectionValidity value) noexcept
	{
		constexpr std::array<std::string_view, 8> names{
			"Valid", "Background", "FieldUnavailable", "Incompatible", "OutsideCoverage",
			"NoValidSupport", "InvalidNumerics", "MalformedPayload" };
		const auto index = static_cast<size_t>(value);
		return index < names.size() ? names[index] : "Unknown";
	}

	[[nodiscard]] inline std::string_view GetGIReceiverCornerReasonName(GIReceiverCornerReason value) noexcept
	{
		constexpr std::array<std::string_view, 5> names{
			"NotEvaluated", "NotActive", "ZeroTrilinearWeight", "ZeroWeight", "Contributing" };
		const auto index = static_cast<size_t>(value);
		return index < names.size() ? names[index] : "Unknown";
	}

	// Eight 16-byte GPU records per corner. Availability is separate from zero.
	struct GIReceiverCorner
	{
		std::array<uint32_t, 3> m_Coordinate{};
		uint32_t m_Classification = 0;
		std::array<float, 3> m_WorldPosition{};
		GIReceiverCornerReason m_Reason = GIReceiverCornerReason::NotEvaluated;
		std::array<float, 3> m_TrilinearFactors{};
		float m_TrilinearWeight = 0;
		std::array<float, 3> m_AtlasM{};
		float m_BackfaceFactor = 0;
		float m_MeanDistance = 0;
		float m_Variance = 0;
		float m_ReceiverDistance = 0;
		float m_Visibility = 0;
		float m_PreVisibilityWeight = 0;
		float m_PreCrushWeight = 0;
		float m_RawWeight = 0;
		float m_NormalizedWeight = 0;
		std::array<float, 3> m_ContributionE{};
		float m_NormalizationAvailable = 0;
		float m_UsedVariance = 0;
		float m_SamplingEvaluated = 0;
		std::array<float, 2> m_Padding{};
	};
	static_assert(sizeof(GIReceiverCorner) == 128 && offsetof(GIReceiverCorner, m_Reason) == 28 &&
		offsetof(GIReceiverCorner, m_RawWeight) == 88 && offsetof(GIReceiverCorner, m_UsedVariance) == 112);

	struct GIReceiverInspectionResult
	{
		GIReceiverInspectionRequest m_Request;
		GIObservationStamp m_Observation;
		GIReceiverViewSnapshot m_View;
		GIReceiverInspectionValidity m_Validity = GIReceiverInspectionValidity::MalformedPayload;
		bool m_CageEvaluated = false;
		uint32_t m_Support = 0;
		std::array<float, 3> m_Position{};
		float m_Coverage = 0;
		std::array<float, 3> m_BiasedPosition{};
		float m_TotalWeight = 0;
		std::array<float, 3> m_ShadingNormal{};
		bool m_Compatible = false;
		std::array<float, 3> m_VertexNormal{};
		float m_ViewBias = 0;
		std::array<float, 3> m_ViewDirection{};
		float m_Spacing = 0;
		std::array<float, 3> m_IrradianceE{};
		std::array<float, 3> m_OrdinaryIrradianceE{};
		float m_OrdinaryWeight = 0;
		std::array<uint32_t, 3> m_Counts{};
		uint32_t m_IrradianceInteriorWidth = 0;
		std::array<GIReceiverCorner, 8> m_Corners{};
	};

	inline constexpr uint32_t GIReceiverInspectionMagic = 0x47524931;
	inline constexpr uint32_t GIReceiverInspectionHeaderRecords = 9;
	inline constexpr uint32_t GIReceiverInspectionBytes = 9 * 16 + 8 * sizeof(GIReceiverCorner);

	[[nodiscard]] inline bool MatchesGIReceiverInspection(const GIReceiverInspectionRequest& request,
		const GIFieldSummary& summary) noexcept
	{
		const auto& view = summary.m_ReceiverView;
		return request.m_ExpectedGeneration != 0 && request.m_ExpectedGeneration == summary.m_Field.m_Generation &&
			(!request.m_ExpectedRefreshEpoch || *request.m_ExpectedRefreshEpoch == summary.m_Field.m_RefreshEpoch) &&
			request.m_ExpectedViewRevision != 0 && request.m_ExpectedViewRevision == view.m_Revision &&
			request.m_ExpectedViewId == view.m_ViewId && request.m_Pixel[0] < view.m_DisplayExtent[0] &&
			request.m_Pixel[1] < view.m_DisplayExtent[1];
	}

	[[nodiscard]] inline std::shared_ptr<const GIReceiverInspectionResult> DecodeGIReceiverInspection(
		GIReceiverInspectionRequest request, GIObservationStamp observation,
		GIReceiverViewSnapshot view, std::span<const uint8_t> bytes) noexcept
	{
		auto result = std::make_shared<GIReceiverInspectionResult>();
		result->m_Request = request;
		result->m_Observation = observation;
		result->m_View = view;
		if (bytes.size() != GIReceiverInspectionBytes || !view.m_Supported ||
			request.m_ExpectedGeneration == 0 || request.m_ExpectedGeneration != observation.m_Field.m_Generation ||
			request.m_ExpectedViewRevision != view.m_Revision || request.m_ExpectedViewId != view.m_ViewId ||
			(request.m_ExpectedRefreshEpoch && *request.m_ExpectedRefreshEpoch != observation.m_Field.m_RefreshEpoch) ||
			request.m_Pixel[0] >= view.m_DisplayExtent[0] || request.m_Pixel[1] >= view.m_DisplayExtent[1] ||
			observation.m_Kind != GIObservationKind::ReceiverContribution || observation.m_ProducingFrame == 0 ||
			observation.m_FieldUpdate <= observation.m_Field.m_EpochOriginUpdate ||
			observation.m_ReadinessUpdate != observation.m_FieldUpdate - observation.m_Field.m_EpochOriginUpdate)
		{
			return result;
		}
		std::array<uint32_t, 4> header{};
		std::memcpy(header.data(), bytes.data(), 16);
		if (header[0] != GIReceiverInspectionMagic || header[1] > uint32_t(GIReceiverInspectionValidity::InvalidNumerics) || header[2] > 1)
		{
			return result;
		}
		result->m_Validity = static_cast<GIReceiverInspectionValidity>(header[1]);
		result->m_CageEvaluated = header[2] != 0;
		result->m_Support = header[3];
		const auto vectorRow = [&](size_t row, std::array<float, 3>& xyz, float* w = nullptr)
			{
				std::array<float, 4> values{};
				std::memcpy(values.data(), bytes.data() + row * 16, 16);
				xyz = { values[0], values[1], values[2] };
				if (w)
				{
					*w = values[3];
				}
			};
		vectorRow(1, result->m_Position, &result->m_Coverage);
		vectorRow(2, result->m_BiasedPosition, &result->m_TotalWeight);
		float compatible = 0;
		vectorRow(3, result->m_ShadingNormal, &compatible);
		result->m_Compatible = compatible == 1;
		vectorRow(4, result->m_VertexNormal, &result->m_ViewBias);
		vectorRow(5, result->m_ViewDirection, &result->m_Spacing);
		vectorRow(6, result->m_IrradianceE);
		vectorRow(7, result->m_OrdinaryIrradianceE, &result->m_OrdinaryWeight);
		std::array<uint32_t, 4> profile{};
		std::memcpy(profile.data(), bytes.data() + 8 * 16, 16);
		result->m_Counts = { profile[0], profile[1], profile[2] };
		result->m_IrradianceInteriorWidth = profile[3];
		std::memcpy(result->m_Corners.data(), bytes.data() + GIReceiverInspectionHeaderRecords * 16,
			result->m_Corners.size() * sizeof(GIReceiverCorner));
		// Integer lanes are checked as integers, not float bit patterns.
		for (size_t row = 1; row < 8; ++row)
		{
			for (size_t lane = 0; lane < 4; ++lane)
			{
				float value = 0;
				std::memcpy(&value, bytes.data() + row * 16 + lane * 4, 4);
				if (!std::isfinite(value))
				{
					result->m_Validity = GIReceiverInspectionValidity::InvalidNumerics;
				}
			}
		}
		if (result->m_CageEvaluated)
		{
			for (const auto& corner : result->m_Corners)
			{
				if (uint32_t(corner.m_Reason) > uint32_t(GIReceiverCornerReason::Contributing) ||
					corner.m_Coordinate[0] >= result->m_Counts[0] || corner.m_Coordinate[1] >= result->m_Counts[1] ||
					corner.m_Coordinate[2] >= result->m_Counts[2])
				{
					result->m_Validity = GIReceiverInspectionValidity::MalformedPayload;
					return result;
				}
				const auto* data = reinterpret_cast<const uint8_t*>(&corner);
				for (size_t offset = 16; offset < sizeof(corner); offset += 4)
				{
					if (offset == 28)
					{
						continue;
					}
					float value = 0;
					std::memcpy(&value, data + offset, 4);
					if (!std::isfinite(value))
					{
						result->m_Validity = GIReceiverInspectionValidity::InvalidNumerics;
					}
				}
			}
		}
		return result;
	}
}
