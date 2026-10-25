#include "Application/Platform/Windows/Win32RHIContextFactory.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/RHI/DX12/DX12ContextFactory.h"
#if GGLAB_ENABLE_VULKAN
#include "GGLabRuntime/Graphics/RHI/Vulkan/VulkanWin32ContextFactory.h"
#endif

#include <memory>

namespace gglab
{
	std::unique_ptr<Win32RHIContextFactory> Win32RHIContextFactory::Create(
		RHIBackendType backend, void* nativeWindowHandle) noexcept
	{
		if (nativeWindowHandle == nullptr)
		{
			return {};
		}
		return std::unique_ptr<Win32RHIContextFactory>(new Win32RHIContextFactory(
			backend, GetModuleHandleW(nullptr), static_cast<HWND>(nativeWindowHandle)));
	}

	Win32RHIContextFactory::Win32RHIContextFactory(
		RHIBackendType backend, HINSTANCE instance, HWND window) noexcept :
		m_Backend(backend), m_Instance(instance), m_Window(window)
	{
	}

	std::unique_ptr<RHIContext> Win32RHIContextFactory::CreateContext(
		const RHIContextDesc& desc) const noexcept
	{
		if (m_Instance == nullptr || m_Window == nullptr)
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"Win32 RHI composition requires a valid process instance and window.");
			return {};
		}

		switch (m_Backend)
		{
		case RHIBackendType::DX12:
			return CreateDX12Context(desc, m_Window);
		case RHIBackendType::Vulkan:
#if GGLAB_ENABLE_VULKAN
			return CreateVulkanWin32Context(desc, m_Instance, m_Window, sizeof(void*) == 8);
#else
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"The Vulkan RHI was requested, but this build has GGLAB_ENABLE_VULKAN=0.");
			return {};
#endif
		default:
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"Win32 RHI composition received an unsupported backend.");
			return {};
		}
	}
}
