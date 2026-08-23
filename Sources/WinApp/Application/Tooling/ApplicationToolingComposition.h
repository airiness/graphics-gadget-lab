#pragma once

#include <filesystem>
#include <memory>

namespace gglab
{
	class ApplicationToolingIntegrationBase;
	class DemoManager;
	class LabRuntimeLocatorBase;
	class PlatformWindow;
	class RHIContext;
	class TaskSystem;

	struct ApplicationToolingCompositionCreateInfo
	{
		PlatformWindow* m_Window = nullptr;
		RHIContext* m_RHIContext = nullptr;
		const TaskSystem* m_TaskSystem = nullptr;
		DemoManager* m_DemoManager = nullptr;
		LabRuntimeLocatorBase* m_LabRuntimeLocator = nullptr;
		std::filesystem::path m_SettingsRoot;
	};

	[[nodiscard]] std::unique_ptr<ApplicationToolingIntegrationBase>
	CreateApplicationToolingIntegration(
		const ApplicationToolingCompositionCreateInfo& createInfo) noexcept;
}
