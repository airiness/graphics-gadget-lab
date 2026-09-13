#pragma once
#include <Windows.h>

namespace gglab
{
	struct VulkanWin32AdapterInspectionDesc
	{
		HINSTANCE m_Instance = nullptr;
		HWND m_Window = nullptr;
		bool m_IsHostAbiSupported = false;
		bool m_RequestValidation = false;
	};

	// Logs adapter capabilities/profile rejection and default selection through
	// the existing bootstrap inspection path. No frame runtime is created.
	// Borrows the host window synchronously; all Vulkan objects are destroyed
	// before returning. Returns zero on success, nonzero on explicit failure.
	[[nodiscard]] int InspectVulkanWin32Adapters(const VulkanWin32AdapterInspectionDesc& desc) noexcept;
}
