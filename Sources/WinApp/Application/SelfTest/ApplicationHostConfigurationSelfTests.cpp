#include "Application/SelfTest/ApplicationHostConfigurationSelfTests.h"

#include "Application/ApplicationHostConfiguration.h"
#include "GGLabFoundation/IO/PathUtils.h"

#include <filesystem>

namespace gglab
{
	void RunApplicationHostConfigurationSelfTests(SelfTestContext& context) noexcept
	{
		ApplicationLaunchOptions options{};
		options.m_StartupDemo = ApplicationStartupDemo::LabHost;
		options.m_StartupLabId = "gglab.lab.culling";
		options.m_StartWithAbsoluteMouse = true;
		options.m_RhiBackend = RHIBackendType::Vulkan;
		options.m_AdapterSelector = "0";
		const AppRuntimeConfig config = TranslateApplicationLaunchOptions(
			options, { 1920, 1080 }, true);
		context.Check(config.IsValid() && config.m_RhiBackend == AppRuntimeRHIBackend::Vulkan &&
			config.m_AdapterSelector == "0" &&
			config.m_StartupDemoId == "Demo.LabHost" &&
			config.m_StartupLabId == "gglab.lab.culling",
			"Windows launch policy translates backend, adapter, demo, and Lab identity explicitly");
		context.Check(config.m_InitialExtent.m_Width == 1920 &&
			config.m_InitialExtent.m_Height == 1080 &&
			config.m_InitialPointerMode == AppRuntimePointerMode::Absolute &&
			config.m_RequestRuntimeValidation,
			"Host translation carries extent, pointer mode, and validation request explicitly");
		context.Check(config.HasCapability(AppRuntimeCapability::DevelopmentTools),
			"Host selects optional development tooling explicitly");
		ApplicationLaunchOptions noToolingOptions = options;
		noToolingOptions.m_DisableDevelopmentTools = true;
		const AppRuntimeConfig noToolingConfig = TranslateApplicationLaunchOptions(
			noToolingOptions, { 1920, 1080 }, true);
		context.Check(!noToolingConfig.HasCapability(AppRuntimeCapability::DevelopmentTools),
			"Host can omit optional development tooling");
		const AppRuntimeConfig defaultContentConfig = TranslateApplicationLaunchOptions(
			ApplicationLaunchOptions{}, { 1920, 1080 }, true);
		context.Check(defaultContentConfig.m_StartupDemoId == "Demo.Start" &&
			defaultContentConfig.m_StartupLabId == "gglab.lab.culling",
			"Windows host policy supplies explicit stable default Demo and Lab identities");

		const std::filesystem::path executableDirectory =
			std::filesystem::temp_directory_path() / "gglab-host-configuration-self-test";
		const std::filesystem::path runtimeRoot = utils::Canonical(executableDirectory);
		const RuntimePaths paths = BuildRuntimePaths(executableDirectory);
		context.Check(paths.IsValid() && paths.m_RuntimeRoot == runtimeRoot &&
			paths.m_AssetRoot == runtimeRoot / "Assets" &&
			paths.m_EnvironmentAssetRoot == runtimeRoot / "Assets" / "Textures" / "Skybox" &&
			paths.m_SettingsRoot == runtimeRoot,
			"Executable directory deterministically produces explicit content roots");
		context.Check(paths.m_ShaderSourceRoot == runtimeRoot / "Shaders" &&
			paths.m_ShaderCacheRoot == runtimeRoot / "ShaderCache" &&
			paths.m_IblDerivedDataRoot == runtimeRoot / "DerivedDataCache" / "IBL" &&
			paths.m_TextureDerivedDataRoot == runtimeRoot / "DerivedDataCache" / "Texture",
			"Executable directory preserves the baseline shader and derived-data roots");
		context.Check(!BuildRuntimePaths("relative/runtime").IsValid(),
			"Host path translation rejects a relative executable directory");

		AppRuntimeConfig invalidAdapterConfig = config;
		invalidAdapterConfig.m_RhiBackend = AppRuntimeRHIBackend::DX12;
		context.Check(!invalidAdapterConfig.IsValid(),
			"Shared runtime config rejects a Vulkan adapter selector for DX12");
	}
}
