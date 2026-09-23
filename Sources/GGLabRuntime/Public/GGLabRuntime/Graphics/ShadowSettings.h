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
	inline constexpr float DefaultDirectionalShadowReceiverDepthBias = 0.0f;
	inline constexpr int32_t DefaultDirectionalShadowRasterizerDepthBias = 360;
	inline constexpr float DefaultDirectionalShadowRasterizerSlopeScaledDepthBias = 0.6f;

	enum class DirectionalShadowFitMode : uint8_t
	{
		Tight,
		StableSphere,
	};

	// Raw raster bias retains backend/format units; scaled bias is resolved in receiver depth.
	enum class DirectionalShadowBiasMode : uint8_t
	{
		LegacyRaw,
		CascadeScaled,
	};

	struct DirectionalShadowResolvedBias
	{
		DirectionalShadowBiasMode m_Mode = DirectionalShadowBiasMode::LegacyRaw;
		float m_WorldUnitsPerTexel = 0.0f;
		float m_DepthSpan = 0.0f;
		float m_ReceiverConstantWorld = 0.0f;
		float m_ReceiverSlopeWorld = 0.0f;
		float m_ReceiverDepthBias = 0.0f;
		float m_ReceiverSlopeDepthBias = 0.0f;
		float m_ReceiverMaxSlope = 0.0f;
		int32_t m_RasterizerDepthBias = 0;
		float m_RasterizerSlopeScaledDepthBias = 0.0f;
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

		DirectionalShadowBiasMode m_BiasMode = DirectionalShadowBiasMode::CascadeScaled;
		// Receiver offset in shadow texels along light-space Z, before depth normalization.
		float m_ReceiverBiasTexels = 0.5f;
		// Optional residual slope bias; receiver-plane/filter correction is automatic.
		float m_ReceiverSlopeBiasTexels = 0.0f;
		float m_ReceiverMaxSlope = 4.0f;

		// Legacy Raw controls are preserved when switching policies for A/B comparisons.
		float m_ReceiverDepthBias = DefaultDirectionalShadowReceiverDepthBias;
		int32_t m_RasterizerDepthBias = DefaultDirectionalShadowRasterizerDepthBias;
		float m_RasterizerSlopeScaledDepthBias =
			DefaultDirectionalShadowRasterizerSlopeScaledDepthBias;
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
