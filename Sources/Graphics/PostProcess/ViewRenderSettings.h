#pragma once

#include <cstdint>

namespace gglab
{
	class Camera;

	enum class ToneMappingOperator : uint8_t
	{
		AcesFitted,
	};

	struct ToneMappingSettings
	{
		ToneMappingOperator m_Operator = ToneMappingOperator::AcesFitted;
	};

	struct BloomSettings
	{
		bool m_Enabled = true;
		float m_Threshold = 1.0f;
		float m_SoftKnee = 0.5f;
		float m_Intensity = 0.08f;
		float m_Scatter = 0.7f;
		uint32_t m_MaxLevels = 6;
	};

	struct PostProcessProfile
	{
		BloomSettings m_Bloom{};
		ToneMappingSettings m_ToneMapping{};
	};

	enum class ForwardLightingMode : uint8_t
	{
		Legacy,
		ForwardPlus,
	};

	struct ForwardPlusSettings
	{
		ForwardLightingMode m_Mode = ForwardLightingMode::ForwardPlus;
		bool m_EnableHdrDiffValidation = false;
	};

	struct LightingProfile
	{
		ForwardPlusSettings m_ForwardPlus{};
	};

	// Authoring settings owned above the renderer by the active Demo or Lab.
	struct ViewRenderProfile
	{
		LightingProfile m_Lighting{};
		PostProcessProfile m_PostProcess{};
	};

	struct ResolvedExposureSettings
	{
		float m_CompensationEV = 0.0f;
		float m_ExposureScale = 1.0f;
	};

	struct ResolvedPostProcessSettings
	{
		BloomSettings m_Bloom{};
		ToneMappingSettings m_ToneMapping{};
	};

	struct ResolvedLightingSettings
	{
		ForwardPlusSettings m_ForwardPlus{};
	};

	// Immutable settings resolved for one RenderView and one frame.
	struct ResolvedViewRenderSettings
	{
		ResolvedExposureSettings m_Exposure{};
		ResolvedLightingSettings m_Lighting{};
		ResolvedPostProcessSettings m_PostProcess{};
	};

	[[nodiscard]] ResolvedViewRenderSettings ResolveViewRenderSettings(
		const ViewRenderProfile& profile, const Camera& camera) noexcept;
}
