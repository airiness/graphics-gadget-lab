#include "DevTools/DevelopGui/DevelopGuiBackendFactory.h"
#include "Application/Platform/Windows/Win32Window.h"
#include "Core/Log/LogMacros.h"
#include "DevTools/DevelopGui/Backends/DX12/DevelopGuiDX12RenderBackend.h"
#include "DevTools/DevelopGui/Backends/Windows/DevelopGuiWin32PlatformBackend.h"
#include "Graphics/RHI/DX12/DX12Context.h"
#if GGLAB_ENABLE_VULKAN
#include "DevTools/DevelopGui/Backends/Vulkan/DevelopGuiVulkanRenderBackend.h"
#include "Graphics/RHI/Vulkan/VulkanContext.h"
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
		if (dynamic_cast<DX12Context*>(&context))
		{
			return std::make_unique<DevelopGuiDX12RenderBackend>();
		}
#if GGLAB_ENABLE_VULKAN
		if (dynamic_cast<VulkanContext*>(&context))
		{
			return std::make_unique<DevelopGuiVulkanRenderBackend>();
		}
#endif

		GGLAB_LOG_GRAPHICS_WARN(
			"No DevelopGui render backend is registered for the current RHI backend.");
		return nullptr;
	}
}
