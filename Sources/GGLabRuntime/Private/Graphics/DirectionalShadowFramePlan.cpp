#include "GGLabRuntime/Graphics/DirectionalShadowFramePlan.h"

#include <algorithm>
#include <cmath>

namespace gglab
{
	namespace
	{
		DirectionalShadowResolvedBias ResolveDirectionalShadowBias(
			const DirectionalShadowViewBuildResult& shadowView,
			const DirectionalShadowSettings& settings) noexcept
		{
			DirectionalShadowResolvedBias result{};
			const auto& texelSize = shadowView.m_Projection.m_WorldUnitsPerTexel;
			result.m_WorldUnitsPerTexel = std::max(texelSize.m_X, texelSize.m_Y);
			result.m_DepthSpan = std::max(shadowView.m_View.m_Far - shadowView.m_View.m_Near, 0.001f);
			result.m_ReceiverConstantWorld = std::max(settings.m_ReceiverBiasTexels, 0.0f) *
				result.m_WorldUnitsPerTexel;
			result.m_ReceiverSlopeWorld = std::max(settings.m_ReceiverSlopeBiasTexels, 0.0f) *
				result.m_WorldUnitsPerTexel;
			result.m_ReceiverDepthBias = result.m_ReceiverConstantWorld / result.m_DepthSpan;
			result.m_ReceiverSlopeDepthBias = result.m_ReceiverSlopeWorld / result.m_DepthSpan;
			result.m_ReceiverMaxSlope = std::clamp(settings.m_ReceiverMaxSlope, 0.0f, 16.0f);
			// Receiver depth bias follows each cascade's footprint without coupling
			// the shadow-map producer to backend-specific raster bias units.
			return result;
		}
	}

	DirectionalShadowFramePlan BuildDirectionalShadowFramePlan(const RenderView& mainView,
		const Vector3& lightDirection, const DirectionalShadowSettings& authoredSettings, bool previewRequested) noexcept
	{
		DirectionalShadowFramePlan result{};
		auto& settings = result.m_Settings;
		settings = authoredSettings;
		result.m_LightDirection = lightDirection;
		settings.m_ShadowMapSize = std::max(settings.m_ShadowMapSize, 1u);
		settings.m_CascadeCount = std::clamp(settings.m_CascadeCount, 1u, MaxDirectionalShadowCascades);
		settings.m_SplitLambda = std::clamp(settings.m_SplitLambda, 0.0f, 1.0f);
		settings.m_CascadeBlendFraction = std::clamp(settings.m_CascadeBlendFraction, 0.0f, 0.5f);
		settings.m_DistanceFadeFraction = std::clamp(settings.m_DistanceFadeFraction, 0.0f, 0.5f);
		result.m_ShadingEnabled = settings.m_Enable;
		result.m_PreviewRequested = previewRequested;
		if (!settings.m_Enable && !previewRequested)
		{
			return result;
		}
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
			receiverView.m_Near = index == 0 ? splitNear : result.m_Cascades.back().m_BlendStart;
			receiverView.m_Far = splitFar;
			const auto shadowView = BuildDirectionalShadowView({
				.m_MainView = receiverView,
				.m_LightDirection = lightDirection,
				.m_ShadowMapSize = std::max(settings.m_ShadowMapSize, 1u),
				.m_MaxShadowDistance = farDepth,
				.m_CasterExtrusionDistance = settings.m_CasterExtrusionDistance,
				.m_OrthoPadding = settings.m_OrthoPadding,
				.m_DepthPadding = settings.m_DepthPadding,
				.m_FilterSupportTexels = settings.m_EnablePCF ? 2.0f : 1.0f,
				.m_FitMode = settings.m_FitMode,
				.m_EnableTexelSnapping = settings.m_EnableTexelSnapping,
			});
			result.m_Cascades.push_back({
				.m_View = shadowView.m_View,
				.m_SplitNear = splitNear,
				.m_SplitFar = splitFar,
				.m_BlendStart = splitFar - (splitFar - splitNear) * settings.m_CascadeBlendFraction,
				.m_Projection = shadowView.m_Projection,
				.m_Bias = ResolveDirectionalShadowBias(shadowView, settings),
			});
			splitNear = splitFar;
		}
		const auto& last = result.m_Cascades.back();
		const float fadeWidth = (last.m_SplitFar - last.m_SplitNear) * settings.m_DistanceFadeFraction;
		result.m_DistanceFadeStart = last.m_SplitFar - fadeWidth;
		result.m_DistanceFadeInvRange = fadeWidth > 0.0f ? 1.0f / fadeWidth : 0.0f;
		return result;
	}

	DirectionalShadowGPU BuildDirectionalShadowGPU(
		const DirectionalShadowFramePlan& cascades, uint32_t viewBaseOffset) noexcept
	{
		DirectionalShadowGPU result{};
		if (cascades.m_Cascades.empty() || viewBaseOffset == DirectionalShadowFramePlan::UnassignedViewBaseOffset)
		{
			return result;
		}
		GGLAB_ASSERT(cascades.m_Cascades.size() <= MaxDirectionalShadowCascades);
		result.CascadeCount = std::min(
			static_cast<uint32_t>(cascades.m_Cascades.size()), MaxDirectionalShadowCascades);
		result.ViewBaseIndex = viewBaseOffset;
		result.DistanceFadeStart = cascades.m_DistanceFadeStart;
		result.DistanceFadeInvRange = cascades.m_DistanceFadeInvRange;
		result.MainViewIndex = static_cast<uint32_t>(RenderViewID::Main);
		result.NearDepth = cascades.m_Cascades.front().m_SplitNear;
		for (uint32_t index = 0; index < result.CascadeCount; ++index)
		{
			const auto& cascade = cascades.m_Cascades[index];
			result.SplitFar[index] = cascade.m_SplitFar;
			result.BlendStart[index] = cascade.m_BlendStart;
			result.ReceiverDepthBias[index] = cascade.m_Bias.m_ReceiverDepthBias;
			result.ReceiverSlopeDepthBias[index] = cascade.m_Bias.m_ReceiverSlopeDepthBias;
			result.ReceiverMaxSlope[index] = cascade.m_Bias.m_ReceiverMaxSlope;
		}
		return result;
	}
}
