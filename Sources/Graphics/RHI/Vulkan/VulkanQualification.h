#pragma once
#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace gglab
{
	struct VulkanQualificationOptions
	{
		HINSTANCE m_HInstance = nullptr;
		HWND m_Hwnd = nullptr;
		bool m_RequestValidation = false;
		bool m_ListAdapters = false;
		std::optional<std::string> m_AdapterSelector;
		std::filesystem::path m_ShaderSourceRoot;
		std::filesystem::path m_ShaderCacheRoot;
	};

	// Runs the deterministic Vulkan bootstrap, adapter inspection and
	// minimal-frame/resource qualification path. This backend API never
	// selects or falls back to another RHI.
	[[nodiscard]] int RunVulkanQualification(const VulkanQualificationOptions& options) noexcept;
}
