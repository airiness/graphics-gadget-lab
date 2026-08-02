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

	enum class GTAOFinalAOFormatPreference : uint8_t
	{
		PreferR8Unorm,
		ForceR16Float,
	};

	struct GTAOSettings
	{
		bool m_Enabled = true;
		float m_Radius = 1.0f;
		float m_FalloffStart = 0.1f;
		float m_FalloffEnd = 1.0f;
		float m_Thickness = 0.25f;
		uint32_t m_DirectionCount = 2;
		uint32_t m_StepCount = 4;
		uint32_t m_DenoiseRadius = 3;
		GTAOFinalAOFormatPreference m_FinalAOFormatPreference =
			GTAOFinalAOFormatPreference::PreferR8Unorm;
	};

	struct LightingProfile
	{
		ForwardPlusSettings m_ForwardPlus{};
		GTAOSettings m_GTAO{};
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
		GTAOSettings m_GTAO{};
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
