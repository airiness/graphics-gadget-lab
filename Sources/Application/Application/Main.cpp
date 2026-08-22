#include "Application/Application.h"
#include "Application/ApplicationHostConfiguration.h"
#include "Application/ApplicationLaunchOptions.h"
#include "Application/Platform/Windows/Win32PlatformHost.h"
#include "Application/RenderingStartup.h"
#include "Application/SelfTest/SelfTestRunner.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "GGLabFoundation/Platform/Win/Win32TaskWorkerLifecycle.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

int main(int argc, char* argv[])
{
	std::vector<std::string_view> arguments;
	arguments.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
	for (int index = 1; index < argc; ++index)
	{
		arguments.emplace_back(argv[index]);
	}

	const auto launchResult = gglab::ParseApplicationLaunchOptions(arguments);
	if (!launchResult.IsValid())
	{
		std::fprintf(stderr, "Error: %s\n\n%s", launchResult.m_Error.c_str(),
			gglab::GetApplicationLaunchUsage().data());
		return EXIT_FAILURE;
	}
	if (launchResult.m_ShowHelp)
	{
		std::fputs(gglab::GetApplicationLaunchUsage().data(), stdout);
		return EXIT_SUCCESS;
	}
	if (launchResult.m_Options.m_SelfTestSelection &&
		*launchResult.m_Options.m_SelfTestSelection !=
			gglab::ApplicationPathCompositionSelfTestSelection)
	{
		return gglab::RunApplicationSelfTests(*launchResult.m_Options.m_SelfTestSelection)
			? EXIT_SUCCESS
			: EXIT_FAILURE;
	}

	HINSTANCE hInstance = GetModuleHandle(nullptr);
	const gglab::RuntimePaths runtimePaths =
		gglab::BuildRuntimePaths(gglab::win32::GetExecutableDirectory());
	if (launchResult.m_Options.m_SelfTestSelection)
	{
		return gglab::RunApplicationPathCompositionSelfTest(runtimePaths)
			? EXIT_SUCCESS
			: EXIT_FAILURE;
	}
	constexpr gglab::AppRuntimeExtent InitialExtent{ 1920, 1080 };
#if defined(BUILD_DEBUG)
	constexpr bool RequestRuntimeValidation = true;
#else
	constexpr bool RequestRuntimeValidation = false;
#endif
	if (launchResult.m_Options.m_ListAdapters ||
		launchResult.m_Options.m_RunVulkanQualification)
	{
		return gglab::RunRenderingStartupPath(launchResult.m_Options, runtimePaths,
			hInstance, RequestRuntimeValidation);
	}

	gglab::Application::CreateInfo createInfo{};
	createInfo.m_WindowName = L"GraphicsGadgetLab";
	createInfo.m_PlatformHost = std::make_unique<gglab::Win32PlatformHost>(hInstance);
	createInfo.m_RuntimeConfig = gglab::TranslateApplicationLaunchOptions(
		launchResult.m_Options, InitialExtent, RequestRuntimeValidation);
	createInfo.m_RuntimePaths = runtimePaths;
	createInfo.m_HostServices.m_TaskWorkerLifecycle =
		std::make_shared<gglab::win32::Win32TaskWorkerLifecycle>();

	gglab::Application application(std::move(createInfo));
	if (!application.Initialize())
	{
		return EXIT_FAILURE;
	}
	application.Run();
	const int exitCode = application.GetExitCode();
	application.Shutdown();

	return exitCode == 0 ? EXIT_SUCCESS : exitCode;
}
