#pragma once
#include "GGLabRuntime/Graphics/Pipeline/PipelinePresets.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gglab
{
	inline constexpr uint32_t GIProbeVisualizationMaximumBudget = 16384;
	inline constexpr uint32_t GIProbeVisualizationFilterVersion = 1;
	inline constexpr uint32_t GIProbeVisualizationMagic = 0x47505631;

	enum class GIProbeVisualizationMode : uint8_t
	{
		Classification,
		Irradiance,
		Change,
	};

	[[nodiscard]] constexpr std::string_view GetGIProbeVisualizationModeName(GIProbeVisualizationMode mode) noexcept
	{
		return mode == GIProbeVisualizationMode::Change ? "change" :
			mode == GIProbeVisualizationMode::Irradiance ? "irradiance" : "classification";
	}

	enum class GIProbeDepthMode : uint8_t
	{
		XRay,
		SceneDepth,
	};

	[[nodiscard]] constexpr std::string_view GetGIProbeDepthModeName(GIProbeDepthMode mode) noexcept
	{
		return mode == GIProbeDepthMode::SceneDepth ? "scene-depth" : "xray";
	}

	[[nodiscard]] constexpr DepthPreset GetGIProbeDepthPreset(bool reversed) noexcept
	{
		return reversed ? DepthPreset::ReversedZStrictWrite : DepthPreset::StandardZWrite;
	}

	inline constexpr float GIProbeVisualizationMinimumExposureEV = -20.0f;
	inline constexpr float GIProbeVisualizationMaximumExposureEV = 8.0f;

	struct GIProbeSelection
	{
		std::array<uint32_t, 3> m_Coordinate{};
		uint64_t m_ExpectedGeneration = 0;
		bool operator==(const GIProbeSelection&) const = default;
	};

	inline constexpr uint32_t GIProbeVisualizationMaximumHighlights = 1;

	// Classification bits: Pending, Active, Inactive, Invalid. Slice axis 0 disables
	// slicing; 1/2/3 select X/Y/Z. Distance zero disables the distance filter.
	struct GIProbeVisualizationSettings
	{
		bool m_Enabled = false;
		bool m_Bounds = true;
		float m_Radius = 0.12f;
		uint32_t m_Budget = 4096;
		uint32_t m_SliceAxis = 0;
		uint32_t m_LayerMinimum = 0;
		uint32_t m_LayerMaximum = 65535;
		uint32_t m_ClassificationMask = 15;
		float m_MaximumDistance = 0;
		GIProbeVisualizationMode m_Mode = GIProbeVisualizationMode::Classification;
		float m_PreviewExposureEV = 0;
		bool m_ChangeNormalized = true;
		float m_ChangeRange = 3;
		bool m_ChangeBlocks = false;
		GIProbeDepthMode m_DepthMode = GIProbeDepthMode::XRay;
		// A diagnostic overlay, independent of ordinary filters, IDs and private depth winners.
		std::optional<GIProbeSelection> m_Selection;
		bool operator==(const GIProbeVisualizationSettings&) const = default;
	};

	struct GIProbeVisualizationCounts
	{
		uint32_t m_Enumerated = 0;
		uint32_t m_AfterSlice = 0;
		uint32_t m_AfterClassification = 0;
		uint32_t m_AfterDistance = 0;
		uint32_t m_AfterFrustum = 0;
		uint32_t m_Rendered = 0;
		uint32_t m_BudgetDropped = 0;
		uint32_t m_Highlights = 0;
	};

	[[nodiscard]] GIProbeVisualizationSettings ResolveGIProbeVisualizationSettings(
		GIProbeVisualizationSettings settings) noexcept;
}
