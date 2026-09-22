#include "GGLabRuntime/Graphics/DirectionalShadowCascadeSet.h"

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
			result.m_Mode = settings.m_BiasMode;
			const auto& texelSize = shadowView.m_Projection.m_WorldUnitsPerTexel;
			result.m_WorldUnitsPerTexel = std::max(texelSize.m_X, texelSize.m_Y);
			result.m_DepthSpan = std::max(shadowView.m_View.m_Far - shadowView.m_View.m_Near, 0.001f);
			if (settings.m_BiasMode == DirectionalShadowBiasMode::LegacyRaw)
			{
				result.m_ReceiverDepthBias = settings.m_ReceiverDepthBias;
				result.m_ReceiverConstantWorld = result.m_ReceiverDepthBias * result.m_DepthSpan;
				result.m_RasterizerDepthBias = settings.m_RasterizerDepthBias;
				result.m_RasterizerSlopeScaledDepthBias = settings.m_RasterizerSlopeScaledDepthBias;
				return result;
			}

			result.m_ReceiverConstantWorld = std::max(settings.m_ReceiverBiasTexels, 0.0f) *
				result.m_WorldUnitsPerTexel;
			result.m_ReceiverSlopeWorld = std::max(settings.m_ReceiverSlopeBiasTexels, 0.0f) *
				result.m_WorldUnitsPerTexel;
			result.m_ReceiverDepthBias = result.m_ReceiverConstantWorld / result.m_DepthSpan;
			result.m_ReceiverSlopeDepthBias = result.m_ReceiverSlopeWorld / result.m_DepthSpan;
			result.m_ReceiverMaxSlope = std::clamp(settings.m_ReceiverMaxSlope, 0.0f, 16.0f);
			// Apply the portable policy entirely in receiver depth. D32 raster constant bias
			// has backend/format semantics, so do not convert meters into its raw integer.
			// Zero raster terms also avoid stacking two policies or varying PSOs with the camera.
			return result;
		}
	}

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
			const auto shadowView = BuildDirectionalShadowView({
				.m_MainView = receiverView,
				.m_LightDirection = lightDirection,
				.m_ShadowMapSize = std::max(settings.m_ShadowMapSize, 1u),
				.m_MaxShadowDistance = farDepth,
				.m_CasterExtrusionDistance = settings.m_CasterExtrusionDistance,
				.m_OrthoPadding = settings.m_OrthoPadding,
				.m_DepthPadding = settings.m_DepthPadding,
				.m_FitMode = settings.m_FitMode,
				.m_EnableTexelSnapping = settings.m_EnableTexelSnapping,
			});
			result.m_Cascades.push_back({
				.m_View = shadowView.m_View,
				.m_SplitNear = splitNear,
				.m_SplitFar = splitFar,
				.m_Projection = shadowView.m_Projection,
				.m_Bias = ResolveDirectionalShadowBias(shadowView, settings),
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
			const auto& cascade = cascades.m_Cascades[index];
			result.SplitFar[index] = cascade.m_SplitFar;
			result.ReceiverDepthBias[index] = cascade.m_Bias.m_ReceiverDepthBias;
			result.ReceiverSlopeDepthBias[index] = cascade.m_Bias.m_ReceiverSlopeDepthBias;
			result.ReceiverMaxSlope[index] = cascade.m_Bias.m_ReceiverMaxSlope;
		}
		return result;
	}
}
