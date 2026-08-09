#include "Core/Precompiled.h"
#include "Application/RenderingStartup.h"
#include "Graphics/RHI/Vulkan/VulkanQualification.h"

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
		return RunVulkanQualification(qualificationOptions);
	}
}
