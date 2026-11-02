#pragma once
#include <array>
#include <cstdint>

namespace gglab
{
	inline constexpr uint32_t GIProbeVisualizationMaximumBudget = 16384;
	inline constexpr uint32_t GIProbeVisualizationFilterVersion = 1;
	inline constexpr uint32_t GIProbeVisualizationMagic = 0x47505631;

	// Classification bits: Pending, Active, Inactive, Invalid. Slice axis 0 disables
	// slicing; 1/2/3 select X/Y/Z. Distance zero disables the distance filter.
	struct GIProbeVisualizationSettings
	{
		bool m_Enabled = false;
		bool m_Bounds = true;
		float m_Radius = 0.12f;
		uint32_t m_Budget = 2048;
		uint32_t m_SliceAxis = 0;
		uint32_t m_LayerMinimum = 0;
		uint32_t m_LayerMaximum = 65535;
		uint32_t m_ClassificationMask = 15;
		float m_MaximumDistance = 0;
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
	};

	[[nodiscard]] GIProbeVisualizationSettings ResolveGIProbeVisualizationSettings(
		GIProbeVisualizationSettings settings) noexcept;
}
