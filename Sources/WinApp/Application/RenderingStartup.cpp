#include "Application/RenderingStartup.h"
#include "AppRuntimeLog.h"
#if GGLAB_ENABLE_VULKAN
#include "Application/Platform/PlatformHost.h"
#include "Application/Platform/PlatformWindow.h"
#include "Application/Platform/Windows/Win32PlatformHost.h"
#include "Graphics/RHI/Vulkan/VulkanBootstrap.h"
#include "Graphics/RHI/Vulkan/VulkanWin32Surface.h"

#include <optional>
#endif

namespace gglab
{
	int RunRenderingStartupPath(
		const ApplicationLaunchOptions& options, HINSTANCE instance,
		bool requestValidation) noexcept
	{
		InitializeLogging();
		if (!options.m_ListAdapters)
		{
			GGLAB_LOG_ERROR_ALWAYS("Rendering startup requires an adapter-inspection request.");
			return 1;
		}

#if GGLAB_ENABLE_VULKAN
		Win32PlatformHost platformHost(instance);
		if (!platformHost.Initialize({
			.m_Title = L"GraphicsGadgetLab Vulkan Adapter Inspection",
			.m_Width = 1920,
			.m_Height = 1080,
			}))
		{
			GGLAB_LOG_ERROR_ALWAYS("Failed to initialize the rendering startup host.");
			return 1;
		}
		const HWND hwnd = static_cast<HWND>(platformHost.GetMainWindow().GetNativeHandle());

		VulkanWin32SurfaceFactory surfaceFactory(instance, hwnd);
		VulkanBootstrapOptions bootstrapOptions{};
		bootstrapOptions.m_SurfaceFactory = &surfaceFactory;
		bootstrapOptions.m_IsHostAbiSupported = sizeof(void*) == 8;
		bootstrapOptions.m_RequestValidation = requestValidation;
		bootstrapOptions.m_SelectionRequest =
			ParseVulkanAdapterSelectionRequest(std::nullopt);
		VulkanBootstrapReport report;
		const int exitCode = RunVulkanBootstrap(bootstrapOptions, report);
		platformHost.Finalize();
		return exitCode;
#else
		static_cast<void>(instance);
		static_cast<void>(requestValidation);
		GGLAB_LOG_ERROR_ALWAYS("Vulkan backend was not built (GGLAB_ENABLE_VULKAN=0).");
		return 1;
#endif
	}
}
