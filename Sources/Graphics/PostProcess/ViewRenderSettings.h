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

	struct PostProcessProfile
	{
		ToneMappingSettings m_ToneMapping{};
	};

	// Authoring settings owned above the renderer by the active Demo or Lab.
	struct ViewRenderProfile
	{
		PostProcessProfile m_PostProcess{};
	};

	struct ResolvedExposureSettings
	{
		float m_CompensationEV = 0.0f;
		float m_ExposureScale = 1.0f;
	};

	struct ResolvedPostProcessSettings
	{
		ToneMappingSettings m_ToneMapping{};
	};

	// Immutable settings resolved for one RenderView and one frame.
	struct ResolvedViewRenderSettings
	{
		ResolvedExposureSettings m_Exposure{};
		ResolvedPostProcessSettings m_PostProcess{};
	};

	[[nodiscard]] ResolvedViewRenderSettings ResolveViewRenderSettings(
		const ViewRenderProfile& profile,
		const Camera& camera) noexcept;
}
