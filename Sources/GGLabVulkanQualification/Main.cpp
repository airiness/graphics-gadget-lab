#include "Platform/Windows/Win32QualificationHost.h"
#include "QualificationLaunchOptions.h"
#include "SelfTest/SelfTestRunner.h"
#include "VulkanQualification.h"
#include "GGLabFoundation/Logging/Log.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#if GGLAB_ENABLE_VULKAN
#include "Graphics/RHI/Vulkan/VulkanWin32Surface.h"
#endif

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <vector>

int main(int argc, char* argv[])
{
	std::vector<std::string_view> arguments;
	arguments.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
	for (int index = 1; index < argc; ++index)
	{
		arguments.emplace_back(argv[index] != nullptr ? argv[index] : "");
	}

	const gglab::VulkanQualificationLaunchParseResult launchResult =
		gglab::ParseVulkanQualificationLaunchOptions(arguments);
	if (!launchResult.IsValid())
	{
		std::fprintf(stderr, "Error: %s\n\n%s", launchResult.m_Error.c_str(),
			gglab::GetVulkanQualificationUsage().data());
		return EXIT_FAILURE;
	}
	if (launchResult.m_ShowHelp)
	{
		std::fputs(gglab::GetVulkanQualificationUsage().data(), stdout);
		return EXIT_SUCCESS;
	}

	gglab::InitializeLogging();
	if (launchResult.m_Options.m_RunSelfTests)
	{
		return gglab::RunVulkanQualificationSelfTestSuites()
			? EXIT_SUCCESS
			: EXIT_FAILURE;
	}

#if GGLAB_ENABLE_VULKAN
	HINSTANCE instance = GetModuleHandleW(nullptr);
	gglab::Win32QualificationHost host;
	if (!host.Initialize(instance, L"GGLab Vulkan Qualification", 1920, 1080))
	{
		std::fputs("Error: failed to initialize the Vulkan qualification window.\n", stderr);
		return EXIT_FAILURE;
	}

	gglab::VulkanWin32SurfaceFactory surfaceFactory(instance, host.GetWindow());
	const std::filesystem::path runtimeRoot = gglab::win32::GetExecutableDirectory();
	gglab::VulkanQualificationOptions options{};
	options.m_SurfaceFactory = &surfaceFactory;
	options.m_Host = &host;
	options.m_IsHostAbiSupported = sizeof(void*) == 8;
#if defined(BUILD_DEBUG)
	options.m_RequestValidation = true;
#else
	options.m_RequestValidation = false;
#endif
	options.m_AdapterSelector = launchResult.m_Options.m_AdapterSelector;
	options.m_ShaderSourceRoot = runtimeRoot / "Shaders";
	options.m_ShaderCacheRoot = runtimeRoot / "ShaderCache";
	return gglab::RunVulkanQualification(options);
#else
	std::fputs("Error: Vulkan backend was not built (GGLAB_ENABLE_VULKAN=0).\n", stderr);
	return EXIT_FAILURE;
#endif
}
