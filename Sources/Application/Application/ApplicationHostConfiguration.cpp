#include "Application/ApplicationHostConfiguration.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "Graphics/Shader/ShaderPaths.h"

namespace gglab
{
	AppRuntimeConfig TranslateApplicationLaunchOptions(
		const ApplicationLaunchOptions& options, AppRuntimeExtent initialExtent,
		bool requestRuntimeValidation) noexcept
	{
		AppRuntimeConfig config{};
		switch (options.m_RhiBackend)
		{
		case RHIBackendType::DX12:
			config.m_RhiBackend = AppRuntimeRHIBackend::DX12;
			break;
		case RHIBackendType::Vulkan:
			config.m_RhiBackend = AppRuntimeRHIBackend::Vulkan;
			break;
		default:
			config.m_RhiBackend = AppRuntimeRHIBackend::Unknown;
			break;
		}
		config.m_AdapterSelector = options.m_AdapterSelector;
		config.m_StartupLabId = options.m_StartupLabId;
		config.m_InitialExtent = initialExtent;
		config.m_InitialPointerMode = options.m_StartWithAbsoluteMouse
			? AppRuntimePointerMode::Absolute
			: AppRuntimePointerMode::Relative;
		config.m_Capabilities =
			AppRuntimeCapability::BuiltInContent | AppRuntimeCapability::DevelopmentTools;
		config.m_RequestRuntimeValidation = requestRuntimeValidation;

		switch (options.m_StartupDemo)
		{
		case ApplicationStartupDemo::Playground:
			config.m_StartupDemo = AppRuntimeStartupDemo::Playground;
			break;
		case ApplicationStartupDemo::LabHost:
			config.m_StartupDemo = AppRuntimeStartupDemo::LabHost;
			break;
		case ApplicationStartupDemo::Start:
		default:
			config.m_StartupDemo = AppRuntimeStartupDemo::Start;
			break;
		}
		return config;
	}

	RuntimePaths BuildRuntimePaths(const std::filesystem::path& executableDirectory) noexcept
	{
		if (executableDirectory.empty() || !executableDirectory.is_absolute())
		{
			return {};
		}

		const std::filesystem::path runtimeRoot = utils::Canonical(executableDirectory);
		const std::filesystem::path assetRoot = runtimeRoot / "Assets";
		const std::filesystem::path derivedDataRoot = runtimeRoot / "DerivedDataCache";
		return {
			.m_RuntimeRoot = runtimeRoot,
			.m_AssetRoot = assetRoot,
			.m_ShaderSourceRoot = ResolveShaderSourceRoot(runtimeRoot),
			.m_ShaderCacheRoot = ResolveShaderCacheRoot(runtimeRoot),
			.m_IblDerivedDataRoot = derivedDataRoot / "IBL",
			.m_TextureDerivedDataRoot = derivedDataRoot / "Texture",
			.m_EnvironmentAssetRoot = assetRoot / "Textures" / "Skybox",
			.m_SettingsRoot = runtimeRoot,
		};
	}
}
