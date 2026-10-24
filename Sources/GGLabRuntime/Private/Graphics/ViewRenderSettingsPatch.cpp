#include "GGLabRuntime/Graphics/ViewRenderSettingsPatch.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAOTypes.h"

#include <algorithm>
#include <optional>

namespace gglab
{
	namespace
	{
		template<typename T>
		void ApplyOptional(const std::optional<T>& value, T& target) noexcept
		{
			if (value)
			{
				target = *value;
			}
		}
	}

	bool TemporalAASettingsPatch::IsEmpty() const noexcept
	{
		return !m_Enabled && !m_MaxHistoryFeedback && !m_DepthAbsoluteThreshold &&
			!m_DepthRelativeThreshold && !m_VelocityWeightScale && !m_LuminanceWeightScale &&
			!m_NeighborhoodClampExpansion && !m_HistoryRelaxation && !m_HistoryAccumulation &&
			!m_HistoryRectification && !m_VarianceClipGamma && !m_HistoryFilter && !m_CurrentFilter &&
			!m_MotionSelection && !m_PostTemporalView && !m_ResolutionPreset &&
			!m_TextureLodBiasOffset;
	}

	TemporalAASettings ApplyTemporalAASettingsPatch(
		const TemporalAASettingsPatch& patch, TemporalAASettings settings) noexcept
	{
		ApplyOptional(patch.m_Enabled, settings.m_Enabled);
		ApplyOptional(patch.m_MaxHistoryFeedback, settings.m_MaxHistoryFeedback);
		ApplyOptional(patch.m_DepthAbsoluteThreshold, settings.m_DepthAbsoluteThreshold);
		ApplyOptional(patch.m_DepthRelativeThreshold, settings.m_DepthRelativeThreshold);
		ApplyOptional(patch.m_VelocityWeightScale, settings.m_VelocityWeightScale);
		ApplyOptional(patch.m_LuminanceWeightScale, settings.m_LuminanceWeightScale);
		ApplyOptional(patch.m_NeighborhoodClampExpansion, settings.m_NeighborhoodClampExpansion);
		ApplyOptional(patch.m_HistoryRelaxation, settings.m_HistoryRelaxation);
		ApplyOptional(patch.m_HistoryAccumulation, settings.m_HistoryAccumulation);
		ApplyOptional(patch.m_HistoryRectification, settings.m_HistoryRectification);
		ApplyOptional(patch.m_VarianceClipGamma, settings.m_VarianceClipGamma);
		ApplyOptional(patch.m_HistoryFilter, settings.m_HistoryFilter);
		ApplyOptional(patch.m_CurrentFilter, settings.m_CurrentFilter);
		ApplyOptional(patch.m_MotionSelection, settings.m_MotionSelection);
		ApplyOptional(patch.m_PostTemporalView, settings.m_PostTemporalView);
		ApplyOptional(patch.m_ResolutionPreset, settings.m_ResolutionPreset);
		ApplyOptional(patch.m_TextureLodBiasOffset, settings.m_TextureLodBiasOffset);
		return ResolveTemporalAASettings(settings);
	}

	GTAOSettings ApplyGTAOSettingsPatch(
		const GTAOSettingsPatch& patch, GTAOSettings settings) noexcept
	{
		ApplyOptional(patch.m_Enabled, settings.m_Enabled);
		if (patch.m_Radius)
		{
			settings.m_Radius = std::clamp(*patch.m_Radius, GTAOMinRadius, GTAOMaxRadius);
		}
		ApplyOptional(patch.m_DirectionCount, settings.m_DirectionCount);
		ApplyOptional(patch.m_StepCount, settings.m_StepCount);
		ApplyOptional(patch.m_TemporalAccumulation, settings.m_TemporalAccumulation);
		ApplyOptional(patch.m_TemporalMaxSamples, settings.m_TemporalMaxSamples);
		settings.m_DirectionCount = std::clamp(settings.m_DirectionCount, 1u, GTAOMaxDirectionCount);
		settings.m_StepCount = std::clamp(settings.m_StepCount, 1u, GTAOMaxStepCount);
		settings.m_TemporalMaxSamples =
			std::clamp(settings.m_TemporalMaxSamples, 1u, GTAOMaxTemporalSamples);
		settings.m_FalloffStart = std::clamp(settings.m_FalloffStart, 0.0f, settings.m_Radius);
		settings.m_FalloffEnd =
			std::clamp(settings.m_FalloffEnd, settings.m_FalloffStart, settings.m_Radius);
		return settings;
	}

	void ApplyViewRenderSettingsPatch(const ViewRenderSettingsPatch& patch,
		ResolvedViewRenderSettings& settings, bool enablePreExposure) noexcept
	{
		settings.m_TemporalAA = ApplyTemporalAASettingsPatch(patch.m_TemporalAA, settings.m_TemporalAA);
		settings.m_Lighting.m_GTAO =
			ApplyGTAOSettingsPatch(patch.m_Lighting.m_GTAO, settings.m_Lighting.m_GTAO);
		settings.m_Exposure = ResolveManualExposureSettings(
			patch.m_Exposure.m_ManualEV100.value_or(settings.m_Exposure.m_ManualEV100),
			patch.m_Exposure.m_CompensationEV.value_or(settings.m_Exposure.m_CompensationEV),
			enablePreExposure);
	}
}
