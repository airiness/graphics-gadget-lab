#include "Core/Precompiled.h"
#include "Graphics/PostProcess/ViewRenderSettings.h"
#include "Graphics/Camera.h"

namespace gglab
{
	ResolvedViewRenderSettings ResolveViewRenderSettings(
		const ViewRenderProfile& profile,
		const Camera& camera) noexcept
	{
		return {
			.m_Exposure = {
				.m_CompensationEV = camera.GetExposureCompensationEV(),
				.m_ExposureScale = camera.GetExposureMultiplier(),
			},
			.m_PostProcess = {
				.m_ToneMapping = profile.m_PostProcess.m_ToneMapping,
			},
		};
	}
}
