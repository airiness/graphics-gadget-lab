#include "ApplicationToolingIntegration.h"

namespace gglab
{
	ViewRenderProfile ApplicationToolingFrameSettings::ResolveProfile(
		const ViewRenderProfile& authored) const noexcept
	{
		ViewRenderProfile profile = authored;
		if (m_TemporalAA)
		{
			profile.m_TemporalAA = *m_TemporalAA;
		}
		if (m_GTAO)
		{
			profile.m_Lighting.m_GTAO = *m_GTAO;
		}
		if (m_Bloom)
		{
			profile.m_PostProcess.m_Bloom = *m_Bloom;
		}
		profile.m_EnableScenePreExposure =
			m_ScenePreExposure.value_or(authored.m_EnableScenePreExposure);
		return profile;
	}

	ApplicationViewSettingsResolution ResolveApplicationViewSettings(
		const ViewRenderProfile& authored, const Camera& camera,
		const ApplicationToolingFrameSettings& tooling) noexcept
	{
		const auto profile = tooling.ResolveProfile(authored);
		return { profile, ResolveViewRenderSettings(profile, camera) };
	}

	ApplicationToolingIntegrationBase::~ApplicationToolingIntegrationBase() = default;

	ApplicationToolingFrame::ApplicationToolingFrame(
		ApplicationToolingIntegrationBase* integration) noexcept
	{
		if (integration && integration->BeginFrame())
		{
			m_Integration = integration;
		}
	}

	ApplicationToolingFrame::~ApplicationToolingFrame() noexcept
	{
		Abort();
	}

	RenderPipelineOverlayExtensionBase* ApplicationToolingFrame::GetOverlayExtension() const noexcept
	{
		return m_Integration ? m_Integration->GetOverlayExtension() : nullptr;
	}

	void ApplicationToolingFrame::Draw(const ApplicationToolingFrameContext& context) noexcept
	{
		if (m_Integration)
		{
			m_Integration->Draw(context);
		}
	}

	void ApplicationToolingFrame::Complete() noexcept
	{
		Close(ApplicationToolingFrameEndReason::Completed);
	}

	void ApplicationToolingFrame::Abort() noexcept
	{
		Close(ApplicationToolingFrameEndReason::Aborted);
	}

	void ApplicationToolingFrame::Close(ApplicationToolingFrameEndReason reason) noexcept
	{
		if (!m_Integration)
		{
			return;
		}

		ApplicationToolingIntegrationBase* integration = m_Integration;
		m_Integration = nullptr;
		integration->EndFrame(reason);
	}
}
