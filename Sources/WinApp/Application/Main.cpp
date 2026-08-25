#include "Application/Application.h"
#include "Application/ApplicationHostConfiguration.h"
#include "Application/ApplicationLaunchOptions.h"
#include "Application/Content/DesktopApplicationContent.h"
#include "Application/Platform/Windows/Win32PlatformHost.h"
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
#include "Application/RenderingStartup.h"
#endif
#include "Application/SelfTest/SelfTestRunner.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "GGLabFoundation/Platform/Win/Win32TaskWorkerLifecycle.h"
#if defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
#include <nlohmann/json.hpp>
#endif

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
namespace
{
	[[nodiscard]] std::optional<gglab::RHIBackendType> ReadPackagedBackend(
		const std::filesystem::path& runtimeRoot) noexcept
	{
		try
		{
			std::ifstream input(runtimeRoot / "artifact-only-package.json");
			if (!input)
			{
				return std::nullopt;
			}
			const nlohmann::json manifest = nlohmann::json::parse(input, nullptr, false);
			if (manifest.is_discarded() || !manifest.is_object() ||
				manifest.value("schemaVersion", 0) != 1 ||
				manifest.value("packageKind", std::string{}) !=
					"gglab.artifact-only-runtime")
			{
				return std::nullopt;
			}
			const std::string backend = manifest.value("backend", std::string{});
			const std::string shaderTarget =
				manifest.value("shaderTarget", std::string{});
			if (backend == "dx12" && shaderTarget == "gglab-dx12")
			{
				return gglab::RHIBackendType::DX12;
			}
			if (backend == "vulkan" && shaderTarget == "gglab-vulkan13")
			{
				return gglab::RHIBackendType::Vulkan;
			}
		}
		catch (...)
		{
		}
		return std::nullopt;
	}
}
#endif

int main(int argc, char* argv[])
{
	std::vector<std::string_view> arguments;
	arguments.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
	for (int index = 1; index < argc; ++index)
	{
		arguments.emplace_back(argv[index]);
	}

	auto launchResult = gglab::ParseApplicationLaunchOptions(arguments);
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
	const bool isPathSensitiveSelfTest = launchResult.m_Options.m_SelfTestSelection &&
		(*launchResult.m_Options.m_SelfTestSelection ==
			gglab::ApplicationPathCompositionSelfTestSelection ||
			*launchResult.m_Options.m_SelfTestSelection ==
				gglab::ApplicationArtifactPackageClosureSelfTestSelection);
	if (launchResult.m_Options.m_SelfTestSelection && !isPathSensitiveSelfTest)
	{
		return gglab::RunApplicationSelfTests(*launchResult.m_Options.m_SelfTestSelection)
			? EXIT_SUCCESS
			: EXIT_FAILURE;
	}

	HINSTANCE hInstance = GetModuleHandle(nullptr);
	const gglab::RuntimePaths runtimePaths =
		gglab::BuildRuntimePaths(gglab::win32::GetExecutableDirectory());
#if defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
	const std::optional<gglab::RHIBackendType> packagedBackend =
		ReadPackagedBackend(runtimePaths.m_RuntimeRoot);
	if (!packagedBackend)
	{
		std::fputs(
			"Error: artifact-only runtime requires a valid package manifest with one matching backend and shader target.\n",
			stderr);
		return EXIT_FAILURE;
	}
	if (launchResult.m_Options.m_RhiBackendSpecified &&
		launchResult.m_Options.m_RhiBackend != *packagedBackend)
	{
		std::fputs(
			"Error: explicit RHI backend does not match the artifact-only package backend.\n",
			stderr);
		return EXIT_FAILURE;
	}
	launchResult.m_Options.m_RhiBackend = *packagedBackend;
#endif
	if (isPathSensitiveSelfTest)
	{
		const bool succeeded = *launchResult.m_Options.m_SelfTestSelection ==
			gglab::ApplicationPathCompositionSelfTestSelection
			? gglab::RunApplicationPathCompositionSelfTest(
				runtimePaths, launchResult.m_Options.m_RhiBackend)
			: gglab::RunApplicationArtifactPackageClosureSelfTest(
				runtimePaths, launchResult.m_Options.m_RhiBackend);
		return succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
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
#if defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
		std::fputs(
			"Error: adapter listing and standalone Vulkan qualification are development-tool modes omitted from the artifact-only target.\n",
			stderr);
		return EXIT_FAILURE;
#else
		return gglab::RunRenderingStartupPath(launchResult.m_Options, runtimePaths,
			hInstance, RequestRuntimeValidation);
#endif
	}

	gglab::Application::CreateInfo createInfo{};
	createInfo.m_WindowName = L"GraphicsGadgetLab";
	createInfo.m_PlatformHost = std::make_unique<gglab::Win32PlatformHost>(hInstance);
	createInfo.m_RuntimeConfig = gglab::TranslateApplicationLaunchOptions(
		launchResult.m_Options, InitialExtent, RequestRuntimeValidation);
	createInfo.m_RuntimePaths = runtimePaths;
	createInfo.m_ContentRegistration = gglab::CreateDesktopApplicationContent();
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
