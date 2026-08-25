#include "ApplicationToolingIntegration.h"

namespace gglab
{
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
