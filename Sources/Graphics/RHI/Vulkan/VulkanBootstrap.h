#pragma once
#include "Graphics/RHI/Vulkan/VulkanAdapter.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanInstance.h"
#include "Graphics/RHI/Vulkan/VulkanWin32Surface.h"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gglab
{
	enum class VulkanAdapterSelectionKind : uint8_t
	{
		Default,
		Index,
		Prefix,
	};

	struct VulkanAdapterSelectionRequest
	{
		VulkanAdapterSelectionKind m_Kind = VulkanAdapterSelectionKind::Default;
		uint32_t m_Index = 0;
		// Matched case-insensitively against the device name and as a hex
		// prefix against the device UUID.
		std::string m_Prefix;
	};

	enum class VulkanAdapterSelectionStatus : uint8_t
	{
		Selected,
		NoAcceptedAdapter,
		IndexOutOfRange,
		RejectedAdapter,
		SelectorNoMatch,
		SelectorAmbiguous,
	};

	struct VulkanAdapterSelectionResult
	{
		VulkanAdapterSelectionStatus m_Status = VulkanAdapterSelectionStatus::NoAcceptedAdapter;
		uint32_t m_SelectedIndex = 0;

		[[nodiscard]] bool IsSelected() const noexcept
		{
			return m_Status == VulkanAdapterSelectionStatus::Selected;
		}
	};

	// Deterministic, GPU-free adapter selection over the evaluated snapshots.
	[[nodiscard]] VulkanAdapterSelectionResult SelectVulkanAdapter(
		const std::vector<VulkanAdapterCapabilitySnapshot>& snapshots,
		const VulkanAdapterSelectionRequest& request) noexcept;

	struct VulkanBootstrapOptions
	{
		HINSTANCE m_HInstance = nullptr;
		HWND m_Hwnd = nullptr;
		bool m_RequestValidation = false;
		VulkanAdapterSelectionRequest m_SelectionRequest{};
	};

	struct VulkanBootstrapReport
	{
		// Snapshot of every enumerated adapter, including rejected ones.
		std::vector<VulkanAdapterCapabilitySnapshot> m_Adapters;
		// Index into m_Adapters of the selected adapter; valid on success.
		uint32_t m_SelectedAdapterIndex = 0;
		bool m_HasDebugMessenger = false;
	};

	// Creates the instance, optional debug messenger, Win32 surface, queries
	// every physical device, evaluates the frozen profile, selects an adapter
	// and creates its logical device, then reports and cleans up. Returns a
	// process exit code: 0 on success, non-zero on any explicit failure.
	[[nodiscard]] int RunVulkanBootstrap(
		const VulkanBootstrapOptions& options, VulkanBootstrapReport& outReport) noexcept;
}
