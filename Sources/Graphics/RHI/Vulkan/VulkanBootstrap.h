#pragma once
#include <windows.h>

#include "Graphics/RHI/RHITypes.h"
#include "Graphics/RHI/Vulkan/VulkanAdapter.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanFrameRuntime.h"
#include "Graphics/RHI/Vulkan/VulkanInstance.h"
#include "Graphics/RHI/Vulkan/VulkanWin32Surface.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
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

	// The minimal-frame runtime and the borrowed bootstrap objects it runs
	// on. The frame runtime must be destroyed first (it consumes the device
	// queue and the surface), then the surface and the device before the
	// instance they were created from; member order encodes the destruction
	// order (last declared destroys first).
	struct VulkanBootstrapRuntimeResult
	{
		VulkanAdapterCapabilitySnapshot m_SelectedSnapshot;
		std::unique_ptr<VulkanInstance> m_Instance;
		std::unique_ptr<VulkanDevice> m_Device;
		std::unique_ptr<VulkanWin32Surface> m_Surface;
		std::unique_ptr<VulkanFrameRuntime> m_FrameRuntime;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		bool m_HasDebugMessenger = false;
		std::string m_Error;
		VkResult m_Result = VK_SUCCESS;

		[[nodiscard]] bool Succeeded() const noexcept { return m_FrameRuntime != nullptr; }
	};

	struct VulkanBootstrapRuntimeCreateInfo
	{
		VulkanBootstrapOptions m_BootstrapOptions{};
		uint32_t m_FrameSlotCount = 2;
		RHIFormat m_RequestedFormat = RHIFormat::R8G8B8A8Unorm;
		bool m_Vsync = false;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
	};

	// Runs the same qualification as RunVulkanBootstrap but hands the
	// selected runtime objects to the caller instead of destroying them, so
	// the minimal frame runtime can be created on the already-qualified
	// instance/surface/device. The temporary layout-probe device is still
	// destroyed immediately after each probe.
	[[nodiscard]] VulkanBootstrapRuntimeResult CreateVulkanBootstrapRuntime(
		const VulkanBootstrapRuntimeCreateInfo& createInfo) noexcept;
}
