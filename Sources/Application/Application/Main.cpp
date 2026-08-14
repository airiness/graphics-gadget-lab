#include "Application/Application.h"
#include "Application/ApplicationLaunchOptions.h"
#include "Application/Platform/Windows/Win32PlatformHost.h"
#include "Application/SelfTest/SelfTestRunner.h"

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
	if (launchResult.m_Options.m_SelfTestSelection)
	{
		return gglab::RunApplicationSelfTests(*launchResult.m_Options.m_SelfTestSelection)
			? EXIT_SUCCESS
			: EXIT_FAILURE;
	}

	HINSTANCE hInstance = GetModuleHandle(nullptr);

	gglab::Application::CreateInfo createInfo{};
	createInfo.m_WindowName = L"GraphicsGadgetLab";
	createInfo.m_WindowWidth = 1920;
	createInfo.m_WindowHeight = 1080;
	createInfo.m_PlatformHost = std::make_unique<gglab::Win32PlatformHost>(hInstance);
	createInfo.m_LaunchOptions = launchResult.m_Options;

	gglab::Application::CreateApplicationInstance(std::move(createInfo));
	auto* application = gglab::Application::GetInstance();
	if (!application->IsInitialized())
	{
		gglab::Application::DestroyApplicationInstance();
		return EXIT_FAILURE;
	}
	application->Run();
	const int exitCode = application->GetExitCode();
	gglab::Application::DestroyApplicationInstance();

	return exitCode == 0 ? EXIT_SUCCESS : exitCode;
}
