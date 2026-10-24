#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAOTypes.h"
#include "GGLabRuntime/Graphics/ViewRenderSettingsPatch.h"

#include <algorithm>
#include <cmath>

namespace gglab
{
	ResolvedExposureSettings ResolveManualExposureSettings(
		float manualEV100, float compensationEV, bool enablePreExposure) noexcept
	{
		const float manual = std::isfinite(manualEV100) ?
			Camera::ClampManualEV100(manualEV100) :
			0.0f;
		const float compensation = std::isfinite(compensationEV) ?
			Camera::ClampExposureCompensationEV(compensationEV) :
			0.0f;
		const float effective = manual - compensation;
		const float scale = 1.0f /
			(ManualExposureSaturationNormalization * std::exp2(effective));
		return {
			.m_ManualEV100 = manual,
			.m_CompensationEV = compensation,
			.m_EffectiveEV100 = effective,
			.m_ExposureScale = scale,
			.m_PreExposure = enablePreExposure ? scale : 1.0f,
		};
	}

	ResolvedViewRenderSettings ResolveViewRenderSettings(
		const ViewRenderProfile& profile, const Camera& camera) noexcept
	{
		const TemporalAASettings temporalAA = ResolveTemporalAASettings(profile.m_TemporalAA);

		BloomSettings bloom = profile.m_PostProcess.m_Bloom;
		bloom.m_Threshold = std::max(bloom.m_Threshold, 0.0f);
		bloom.m_SoftKnee = std::clamp(bloom.m_SoftKnee, 0.0f, 1.0f);
		bloom.m_Intensity = std::max(bloom.m_Intensity, 0.0f);
		bloom.m_Scatter = std::clamp(bloom.m_Scatter, 0.0f, 1.0f);
		bloom.m_MaxLevels = std::clamp(bloom.m_MaxLevels, 1u, 8u);

		GTAOSettings gtao = profile.m_Lighting.m_GTAO;
		gtao.m_Radius = std::clamp(gtao.m_Radius, GTAOMinRadius, GTAOMaxRadius);
		gtao.m_Power = std::clamp(gtao.m_Power, 0.1f, 8.0f);
		gtao.m_DenoiseRadius = std::clamp(gtao.m_DenoiseRadius, 1u, GTAOMaxDenoiseRadius);
		gtao = ApplyGTAOSettingsPatch({}, gtao);

		return {
			.m_TemporalAA = temporalAA,
			.m_Exposure = ResolveManualExposureSettings(
				camera.GetManualEV100(), camera.GetExposureCompensationEV(),
				profile.m_EnableScenePreExposure),
			.m_Lighting =
				{
					.m_GTAO = gtao,
					.m_EnableAerialPerspective = profile.m_Lighting.m_EnableAerialPerspective,
				},
			.m_PostProcess =
				{
					.m_Bloom = bloom,
					.m_ToneMapping = profile.m_PostProcess.m_ToneMapping,
				},
		};
	}
}
