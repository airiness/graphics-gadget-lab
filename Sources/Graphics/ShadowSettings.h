#pragma once
#include <cstdint>

namespace gglab
{
	inline constexpr uint32_t DefaultDirectionalShadowMapSize = 2048;
	inline constexpr uint32_t DefaultDirectionalShadowMapPreviewSize = 512;
	inline constexpr float DefaultDirectionalShadowMaxDistance = 30.0f;
	inline constexpr float DefaultDirectionalShadowCasterExtrusionDistance = 300.0f;
	inline constexpr float DefaultDirectionalShadowOrthoPadding = 1.0f;
	inline constexpr float DefaultDirectionalShadowDepthPadding = 200.0f;
	inline constexpr float DefaultDirectionalShadowReceiverDepthBias = 0.0f;
	inline constexpr int32_t DefaultDirectionalShadowRasterizerDepthBias = 360;
	inline constexpr float DefaultDirectionalShadowRasterizerSlopeScaledDepthBias = 0.6f;

	struct DirectionalShadowSettings
	{
		bool m_Enable = true;
		bool m_EnablePCF = true;

		uint32_t m_ShadowMapSize = DefaultDirectionalShadowMapSize;

		float m_MaxShadowDistance = DefaultDirectionalShadowMaxDistance;
		float m_CasterExtrusionDistance = DefaultDirectionalShadowCasterExtrusionDistance;
		float m_OrthoPadding = DefaultDirectionalShadowOrthoPadding;
		float m_DepthPadding = DefaultDirectionalShadowDepthPadding;

		float m_ReceiverDepthBias = DefaultDirectionalShadowReceiverDepthBias;
		int32_t m_RasterizerDepthBias = DefaultDirectionalShadowRasterizerDepthBias;
		float m_RasterizerSlopeScaledDepthBias = DefaultDirectionalShadowRasterizerSlopeScaledDepthBias;
	};

	struct ShadowVisualizationSettings
	{
		float m_PreviewMinDepth = 0.0f;
		float m_PreviewMaxDepth = 1.0f;
		bool m_PreviewInvert = false;
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
