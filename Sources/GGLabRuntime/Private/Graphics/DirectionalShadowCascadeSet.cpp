#include "GGLabRuntime/Graphics/DirectionalShadowCascadeSet.h"

#include <algorithm>
#include <cmath>

namespace gglab
{
	DirectionalShadowCascadeSet BuildDirectionalShadowCascades(const RenderView& mainView,
		const Vector3& lightDirection, const DirectionalShadowSettings& settings) noexcept
	{
		DirectionalShadowCascadeSet result{};
		const uint32_t count = std::clamp(settings.m_CascadeCount, 1u, MaxDirectionalShadowCascades);
		const float nearDepth = std::max(mainView.m_Near, 0.001f);
		const float farDepth = std::max(nearDepth + 0.001f,
			std::min(mainView.m_Far, settings.m_MaxShadowDistance));
		const float lambda = std::clamp(settings.m_SplitLambda, 0.0f, 1.0f);
		float splitNear = nearDepth;
		result.m_Cascades.reserve(count);
		for (uint32_t index = 0; index < count; ++index)
		{
			const float fraction = static_cast<float>(index + 1) / static_cast<float>(count);
			const float uniformSplit = nearDepth + (farDepth - nearDepth) * fraction;
			const float logarithmicSplit = nearDepth * std::pow(farDepth / nearDepth, fraction);
			const float splitFar = index + 1 == count ? farDepth
				: uniformSplit + lambda * (logarithmicSplit - uniformSplit);
			RenderView receiverView = mainView;
			receiverView.m_Near = splitNear;
			receiverView.m_Far = splitFar;
			result.m_Cascades.push_back({
				.m_View = RenderViewBuilder{}.Build<RenderViewID::DirectionalShadow>({
					.m_MainView = receiverView,
					.m_LightDirection = lightDirection,
					.m_ShadowMapSize = std::max(settings.m_ShadowMapSize, 1u),
					.m_MaxShadowDistance = farDepth,
					.m_CasterExtrusionDistance = settings.m_CasterExtrusionDistance,
					.m_OrthoPadding = settings.m_OrthoPadding,
					.m_DepthPadding = settings.m_DepthPadding,
				}),
				.m_SplitNear = splitNear,
				.m_SplitFar = splitFar,
			});
			splitNear = splitFar;
		}
		return result;
	}

	DirectionalShadowGPU BuildDirectionalShadowGPU(
		const DirectionalShadowCascadeSet& cascades) noexcept
	{
		DirectionalShadowGPU result{};
		if (cascades.m_Cascades.empty() || !cascades.HasViewRange())
		{
			return result;
		}
		GGLAB_ASSERT(cascades.m_Cascades.size() <= MaxDirectionalShadowCascades);
		result.CascadeCount = std::min(
			static_cast<uint32_t>(cascades.m_Cascades.size()), MaxDirectionalShadowCascades);
		result.ViewBaseIndex = cascades.GetViewIndex(0);
		result.MainViewIndex = static_cast<uint32_t>(RenderViewID::Main);
		result.NearDepth = cascades.m_Cascades.front().m_SplitNear;
		for (uint32_t index = 0; index < result.CascadeCount; ++index)
		{
			result.SplitFar[index] = cascades.m_Cascades[index].m_SplitFar;
		}
		return result;
	}
}
