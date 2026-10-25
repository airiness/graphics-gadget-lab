#include "DevTools/DevelopGui/DevelopGuiBackendFactory.h"
#include "Application/Platform/Windows/Win32Window.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "DevTools/DevelopGui/Backends/DX12/DevelopGuiDX12RenderBackend.h"
#include "DevTools/DevelopGui/Backends/Windows/DevelopGuiWin32PlatformBackend.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#if GGLAB_ENABLE_VULKAN
#include "DevTools/DevelopGui/Backends/Vulkan/DevelopGuiVulkanRenderBackend.h"
#endif

namespace gglab
{
	std::unique_ptr<DevelopGuiPlatformBackend> CreateDevelopGuiPlatformBackend(
		PlatformWindow& window) noexcept
	{
		if (dynamic_cast<Win32Window*>(&window))
		{
			return std::make_unique<DevelopGuiWin32PlatformBackend>();
		}

		GGLAB_LOG_GRAPHICS_WARN(
			"No DevelopGui platform backend is registered for the current platform window.");
		return nullptr;
	}

	std::unique_ptr<DevelopGuiRenderBackend> CreateDevelopGuiRenderBackend(
		RHIContext& context) noexcept
	{
		if (context.GetDevice().GetBackendType() == RHIBackendType::DX12)
		{
			return std::make_unique<DevelopGuiDX12RenderBackend>();
		}
#if GGLAB_ENABLE_VULKAN
		if (context.GetDevice().GetBackendType() == RHIBackendType::Vulkan)
		{
			return std::make_unique<DevelopGuiVulkanRenderBackend>();
		}
#endif

		GGLAB_LOG_GRAPHICS_WARN(
			"No DevelopGui render backend is registered for the current RHI backend.");
		return nullptr;
	}
}
