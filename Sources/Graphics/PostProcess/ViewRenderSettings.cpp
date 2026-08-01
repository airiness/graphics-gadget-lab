#include "Core/Precompiled.h"
#include "Graphics/PostProcess/ViewRenderSettings.h"
#include "Graphics/Camera.h"
#include "Graphics/Pipeline/GTAO.h"

namespace gglab
{
	ResolvedViewRenderSettings ResolveViewRenderSettings(
		const ViewRenderProfile& profile, const Camera& camera) noexcept
	{
		BloomSettings bloom = profile.m_PostProcess.m_Bloom;
		bloom.m_Threshold = std::max(bloom.m_Threshold, 0.0f);
		bloom.m_SoftKnee = std::clamp(bloom.m_SoftKnee, 0.0f, 1.0f);
		bloom.m_Intensity = std::max(bloom.m_Intensity, 0.0f);
		bloom.m_Scatter = std::clamp(bloom.m_Scatter, 0.0f, 1.0f);
		bloom.m_MaxLevels = std::clamp(bloom.m_MaxLevels, 1u, 8u);

		GTAOSettings gtao = profile.m_Lighting.m_GTAO;
		gtao.m_Radius = std::clamp(gtao.m_Radius, 0.01f, 10.0f);
		gtao.m_FalloffStart = std::clamp(gtao.m_FalloffStart, 0.0f, gtao.m_Radius);
		gtao.m_FalloffEnd = std::clamp(gtao.m_FalloffEnd, gtao.m_FalloffStart,
			std::max(gtao.m_Radius, gtao.m_FalloffStart));
		gtao.m_Thickness = std::clamp(gtao.m_Thickness, 0.0f, gtao.m_Radius);
		gtao.m_DirectionCount = std::clamp(gtao.m_DirectionCount, 1u, GTAOMaxDirectionCount);
		gtao.m_StepCount = std::clamp(gtao.m_StepCount, 1u, GTAOMaxStepCount);

		return {
			.m_Exposure =
				{
					.m_CompensationEV = camera.GetExposureCompensationEV(),
					.m_ExposureScale = camera.GetExposureMultiplier(),
				},
			.m_Lighting =
				{
					.m_ForwardPlus = profile.m_Lighting.m_ForwardPlus,
					.m_GTAO = gtao,
				},
			.m_PostProcess =
				{
					.m_Bloom = bloom,
					.m_ToneMapping = profile.m_PostProcess.m_ToneMapping,
				},
		};
	}
}
