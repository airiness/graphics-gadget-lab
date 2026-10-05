#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class ApplicationFrameCapture;

	// Read-only capture state plus the interactive capture settings: readiness
	// gates of the current frame, settled frames, pending requests and the recent
	// capture history with file paths or failure reasons.
	class FrameCapturePanel final : public DevelopGuiPanelBase
	{
	public:
		explicit FrameCapturePanel(ApplicationFrameCapture* frameCapture) noexcept :
			m_FrameCapture(frameCapture)
		{
		}

		std::string_view GetPath() const noexcept override { return "Application/Frame Capture"; }
		std::string_view GetTitle() const noexcept override { return "Frame Capture"; }
		void Draw(DevelopGuiContext& context) noexcept override;
		int32_t GetOrder() const noexcept override { return -80; }

	private:
		ApplicationFrameCapture* m_FrameCapture = nullptr;
	};

	// Briefly shows the most recent finished capture, independent of whether the
	// panel is open.
	void DrawFrameCaptureNotification(const ApplicationFrameCapture& frameCapture) noexcept;
}
