#pragma once

#include <filesystem>
#include <memory>

namespace gglab
{
	class ApplicationFrameCapture;
	class ApplicationToolingIntegrationBase;
	class DemoManager;
	class LabRuntimeLocatorBase;
	class PlatformWindow;
	class RHIContext;

	struct ApplicationToolingCompositionCreateInfo
	{
		PlatformWindow* m_Window = nullptr;
		RHIContext* m_RHIContext = nullptr;
		DemoManager* m_DemoManager = nullptr;
		LabRuntimeLocatorBase* m_LabRuntimeLocator = nullptr;
		// Optional; host-owned and outlives the tooling integration's drawing.
		ApplicationFrameCapture* m_FrameCapture = nullptr;
		std::filesystem::path m_SettingsRoot;
	};

	[[nodiscard]] std::unique_ptr<ApplicationToolingIntegrationBase>
	CreateApplicationToolingIntegration(
		const ApplicationToolingCompositionCreateInfo& createInfo) noexcept;
}
