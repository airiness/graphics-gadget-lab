#include "Application/RenderingStartup.h"
#include "Application/ApplicationLog.h"
#include "Application/Platform/PlatformHost.h"
#include "Application/Platform/PlatformWindow.h"
#include "Application/Platform/Windows/Win32PlatformHost.h"
#include "Application/Rendering/VulkanQualification.h"
#if GGLAB_ENABLE_VULKAN
#include "Application/Platform/Windows/Win32VulkanQualificationHost.h"
#include "Graphics/RHI/Vulkan/VulkanWin32Surface.h"
#endif

namespace gglab
{
	int RunRenderingStartupPath(
		const ApplicationLaunchOptions& options, const RuntimePaths& runtimePaths,
		HINSTANCE instance, bool requestValidation) noexcept
	{
		InitializeLogging();
		if (!runtimePaths.IsValid())
		{
			GGLAB_LOG_ERROR_ALWAYS(
				"Rendering startup requires valid absolute host-supplied runtime paths.");
			return 1;
		}

		Win32PlatformHost platformHost(instance);
		if (!platformHost.Initialize({
			.m_Title = L"GraphicsGadgetLab Vulkan Startup",
			.m_Width = 1920,
			.m_Height = 1080,
			}))
		{
			GGLAB_LOG_ERROR_ALWAYS("Failed to initialize the rendering startup host.");
			return 1;
		}
		const HWND hwnd = static_cast<HWND>(platformHost.GetMainWindow().GetNativeHandle());

		VulkanQualificationOptions qualificationOptions{};
#if GGLAB_ENABLE_VULKAN
		VulkanWin32SurfaceFactory surfaceFactory(instance, hwnd);
		Win32VulkanQualificationHost qualificationHost(hwnd);
		qualificationOptions.m_SurfaceFactory = &surfaceFactory;
		qualificationOptions.m_Host = &qualificationHost;
#else
		static_cast<void>(hwnd);
#endif
		qualificationOptions.m_RequestValidation = requestValidation;
		qualificationOptions.m_ListAdapters = options.m_ListAdapters;
		qualificationOptions.m_AdapterSelector = options.m_AdapterSelector;
		qualificationOptions.m_ShaderSourceRoot = runtimePaths.m_ShaderSourceRoot;
		qualificationOptions.m_ShaderCacheRoot = runtimePaths.m_ShaderCacheRoot;
		const int exitCode = RunVulkanQualification(qualificationOptions);
		platformHost.Finalize();
		return exitCode;
	}
}
