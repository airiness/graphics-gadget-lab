#pragma once
#include <cstdint>

namespace gglab
{
	inline constexpr uint32_t MaxDirectionalShadowCascades = 4;
	inline constexpr uint32_t DefaultDirectionalShadowMapSize = 2048;
	inline constexpr uint32_t DefaultDirectionalShadowMapPreviewSize = 512;
	inline constexpr float DefaultDirectionalShadowMaxDistance = 30.0f;
	inline constexpr float DefaultDirectionalShadowCasterExtrusionDistance = 300.0f;
	inline constexpr float DefaultDirectionalShadowOrthoPadding = 1.0f;
	inline constexpr float DefaultDirectionalShadowDepthPadding = 200.0f;

	enum class DirectionalShadowFitMode : uint8_t
	{
		Tight,
		StableSphere,
	};

	struct DirectionalShadowResolvedBias
	{
		float m_WorldUnitsPerTexel = 0.0f;
		float m_DepthSpan = 0.0f;
		float m_ReceiverConstantWorld = 0.0f;
		float m_ReceiverSlopeWorld = 0.0f;
		float m_ReceiverDepthBias = 0.0f;
		float m_ReceiverSlopeDepthBias = 0.0f;
		float m_ReceiverMaxSlope = 0.0f;
	};

	struct DirectionalShadowSettings
	{
		bool m_Enable = true;
		bool m_EnablePCF = true;

		uint32_t m_ShadowMapSize = DefaultDirectionalShadowMapSize;
		uint32_t m_CascadeCount = MaxDirectionalShadowCascades;
		float m_SplitLambda = 0.65f;
		float m_CascadeBlendFraction = 0.1f;
		float m_DistanceFadeFraction = 0.1f;
		DirectionalShadowFitMode m_FitMode = DirectionalShadowFitMode::StableSphere;
		bool m_EnableTexelSnapping = true;

		float m_MaxShadowDistance = DefaultDirectionalShadowMaxDistance;
		float m_CasterExtrusionDistance = DefaultDirectionalShadowCasterExtrusionDistance;
		float m_OrthoPadding = DefaultDirectionalShadowOrthoPadding;
		float m_DepthPadding = DefaultDirectionalShadowDepthPadding;

		// Receiver offset in shadow texels along light-space Z, before depth normalization.
		float m_ReceiverBiasTexels = 0.5f;
		// Optional residual slope bias; receiver-plane/filter correction is automatic.
		float m_ReceiverSlopeBiasTexels = 0.0f;
		float m_ReceiverMaxSlope = 4.0f;
	};

	struct ShadowVisualizationSettings
	{
		uint32_t m_PreviewCascade = 0;
		float m_PreviewMinDepth = 0.0f;
		float m_PreviewMaxDepth = 1.0f;
		bool m_PreviewInvert = false;
		bool m_ShowCascadeOverlay = false;
		bool m_ShowTransitionOverlay = false;
	};

	inline const DirectionalShadowSettings& DisabledDirectionalShadowSettings() noexcept
	{
		static const DirectionalShadowSettings settings = []() noexcept
			{
				DirectionalShadowSettings result{};
				result.m_Enable = false;
				return result;
			}();
		return settings;
	}

	inline const ShadowVisualizationSettings& DefaultShadowVisualizationSettings() noexcept
	{
		static const ShadowVisualizationSettings settings{};
		return settings;
	}
}
