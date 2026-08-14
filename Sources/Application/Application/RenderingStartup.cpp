#include "Application/RenderingStartup.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "Graphics/RHI/Vulkan/VulkanQualification.h"
#include "Graphics/Shader/ShaderPaths.h"

#include <filesystem>

namespace gglab
{
	int RunRenderingStartupPath(
		const ApplicationLaunchOptions& options, HWND hwnd) noexcept
	{
		VulkanQualificationOptions qualificationOptions{};
		qualificationOptions.m_HInstance = GetModuleHandle(nullptr);
		qualificationOptions.m_Hwnd = hwnd;
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
