#include "Application/RenderingStartup.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "Graphics/RHI/Vulkan/VulkanQualification.h"
#include "Graphics/Shader/ShaderPaths.h"
#if GGLAB_ENABLE_VULKAN
#include "Application/Platform/Windows/Win32VulkanQualificationHost.h"
#include "Graphics/RHI/Vulkan/VulkanWin32Surface.h"
#endif

#include <filesystem>

namespace gglab
{
	int RunRenderingStartupPath(
		const ApplicationLaunchOptions& options, HWND hwnd) noexcept
	{
		VulkanQualificationOptions qualificationOptions{};
#if GGLAB_ENABLE_VULKAN
		VulkanWin32SurfaceFactory surfaceFactory(GetModuleHandle(nullptr), hwnd);
		Win32VulkanQualificationHost qualificationHost(hwnd);
		qualificationOptions.m_SurfaceFactory = &surfaceFactory;
		qualificationOptions.m_Host = &qualificationHost;
#else
		static_cast<void>(hwnd);
#endif
#if defined(BUILD_DEBUG)
		qualificationOptions.m_RequestValidation = true;
#endif
		qualificationOptions.m_ListAdapters = options.m_ListAdapters;
		qualificationOptions.m_AdapterSelector = options.m_AdapterSelector;
		const std::filesystem::path runtimeRoot = win32::GetExecutableDirectory();
		qualificationOptions.m_ShaderSourceRoot = ResolveShaderSourceRoot(runtimeRoot);
		qualificationOptions.m_ShaderCacheRoot = ResolveShaderCacheRoot(runtimeRoot);
		return RunVulkanQualification(qualificationOptions);
	}
}
