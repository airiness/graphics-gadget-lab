#pragma once

#include "GGLabRuntime/Graphics/ViewRenderSettings.h"

#include <cstdint>
#include <optional>

namespace gglab
{
	// Optional CPU settings applied before frame planning. Unset fields inherit
	// the supplied settings; transport names and transaction ownership live above Runtime.
	struct TemporalAASettingsPatch
	{
		// False explicitly disables the Temporal AA request.
		std::optional<bool> m_Enabled;
		std::optional<float> m_MaxHistoryFeedback;
		std::optional<float> m_DepthAbsoluteThreshold;
		std::optional<float> m_DepthRelativeThreshold;
		std::optional<float> m_VelocityWeightScale;
		std::optional<float> m_LuminanceWeightScale;
		std::optional<float> m_NeighborhoodClampExpansion;
		std::optional<float> m_HistoryRelaxation;
		std::optional<TemporalAAHistoryAccumulation> m_HistoryAccumulation;
		std::optional<TemporalAAHistoryRectification> m_HistoryRectification;
		std::optional<float> m_VarianceClipGamma;
		std::optional<TemporalAAHistoryFilter> m_HistoryFilter;
		std::optional<TemporalAACurrentFilter> m_CurrentFilter;
		std::optional<TemporalAAMotionSelection> m_MotionSelection;
		std::optional<TemporalAAPostTemporalView> m_PostTemporalView;
		std::optional<TemporalAAResolutionPreset> m_ResolutionPreset;
		std::optional<float> m_TextureLodBiasOffset;

		[[nodiscard]] bool IsEmpty() const noexcept;
	};

	struct GTAOSettingsPatch
	{
		std::optional<bool> m_Enabled;
		std::optional<float> m_Radius;
		std::optional<uint32_t> m_DirectionCount;
		std::optional<uint32_t> m_StepCount;
		std::optional<bool> m_TemporalAccumulation;
		std::optional<uint32_t> m_TemporalMaxSamples;
	};

	struct ExposureSettingsPatch
	{
		std::optional<float> m_ManualEV100;
		std::optional<float> m_CompensationEV;
	};

	struct LightingSettingsPatch
	{
		GTAOSettingsPatch m_GTAO;
	};

	struct ViewRenderSettingsPatch
	{
		TemporalAASettingsPatch m_TemporalAA;
		ExposureSettingsPatch m_Exposure;
		LightingSettingsPatch m_Lighting;
	};

	// TAA retains its existing finite/range/enum resolution. GTAO resolves the
	// sampling bounds and radius-dependent falloff; other GTAO fields are preserved.
	[[nodiscard]] TemporalAASettings ApplyTemporalAASettingsPatch(
		const TemporalAASettingsPatch& patch, TemporalAASettings settings) noexcept;
	[[nodiscard]] GTAOSettings ApplyGTAOSettingsPatch(
		const GTAOSettingsPatch& patch, GTAOSettings settings) noexcept;
	// The baseline is resolved view settings. Exposure and pre-exposure are derived
	// together, including when only one exposure input is patched.
	void ApplyViewRenderSettingsPatch(const ViewRenderSettingsPatch& patch,
		ResolvedViewRenderSettings& settings, bool enablePreExposure) noexcept;
}
