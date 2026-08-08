#pragma once
#include "Graphics/RHI/Vulkan/VulkanDeviceProfile.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gglab
{
	class VulkanWin32Surface;

	// Stable physical device identity. Device and driver UUIDs are the
	// persistent identifiers used by the adapter selector.
	struct VulkanAdapterIdentity
	{
		uint32_t m_EnumerationIndex = 0;
		std::string m_DeviceName;
		VkPhysicalDeviceType m_DeviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER;
		uint32_t m_VendorId = 0;
		uint32_t m_DeviceId = 0;
		uint32_t m_ApiVersion = 0;
		uint32_t m_DriverVersion = 0;
		std::string m_DriverName;
		std::string m_DriverInfo;
		std::array<uint8_t, VK_UUID_SIZE> m_DeviceUuid{};
		std::array<uint8_t, VK_UUID_SIZE> m_DriverUuid{};

		[[nodiscard]] std::string UuidHex() const noexcept;
	};

	struct VulkanFormatSupportDiagnostic
	{
		std::string m_FormatName;
		VkFormat m_Format = VK_FORMAT_UNDEFINED;
		bool m_Supported = false;
		std::string m_Usage;
	};

	// Complete CPU capability snapshot of one physical device, gathered from
	// native Vulkan queries and evaluated through the frozen
	// EvaluateVulkanDeviceProfile contract.
	struct VulkanAdapterCapabilitySnapshot
	{
		VulkanAdapterIdentity m_Identity{};

		bool m_HasGraphicsPresentQueueFamily = false;
		uint32_t m_GraphicsPresentQueueFamilyIndex = 0;
		uint32_t m_GraphicsPresentQueueCount = 0;

		std::vector<std::string> m_AvailableDeviceExtensions;
		std::vector<std::string> m_MissingRequiredDeviceExtensions;

		VulkanDeviceProfileCapabilities m_ProfileCapabilities{};
		VulkanDescriptorCapacityLimits m_DescriptorLimits{};
		VulkanDescriptorCapacityAvailability m_DescriptorCapacityAvailability{};
		std::vector<VulkanFormatSupportDiagnostic> m_FormatDiagnostics;
		VulkanDeviceProfileEvaluation m_ProfileEvaluation{};
		// Distinguishes "the layout probe was never executed" from "support
		// was determined unavailable". A probe is only performed for adapters
		// that pass every other profile requirement. An adapter whose probe
		// device could not be created is not selectable: probed stays false
		// and the failure reason is recorded in m_LayoutProbeError.
		bool m_GlobalDescriptorSetLayoutProbed = false;
		std::string m_LayoutProbeError;
	};

	// Fills the physical-device level part of the capability snapshot. The
	// global descriptor-set layout probe requires a device handle and is
	// completed separately by ProbeGlobalDescriptorSetLayoutSupport.
	[[nodiscard]] VulkanAdapterCapabilitySnapshot QueryVulkanAdapterCapabilitySnapshot(
		VkInstance instance, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
		uint32_t enumerationIndex) noexcept;

	// Probes whether the exact GGLab set-1 descriptor-set layout
	// (mutable sampled/storage images at binding 0, samplers at binding 1,
	// PARTIALLY_BOUND | UPDATE_AFTER_BIND | UPDATE_UNUSED_WHILE_PENDING,
	// UPDATE_AFTER_BIND_POOL) is supported. Requires an enabled
	// VK_EXT_mutable_descriptor_type device.
	[[nodiscard]] bool ProbeGlobalDescriptorSetLayoutSupport(VkDevice device) noexcept;

	// Evaluates a snapshot against the frozen profile and stores the complete
	// rejection reason list.
	inline void EvaluateVulkanAdapterProfile(
		VulkanAdapterCapabilitySnapshot& snapshot) noexcept
	{
		snapshot.m_ProfileEvaluation =
			EvaluateVulkanDeviceProfile(snapshot.m_ProfileCapabilities);
	}

	// Preliminary evaluation used before the descriptor-set layout probe:
	// the layout-support gate is neutralized because the probe has not run
	// yet, so "not probed" never reports as "unsupported". An adapter that
	// fails every other requirement is rejected without creating a probe
	// device; the final evaluation only reports
	// GlobalDescriptorSetLayoutUnsupported after an actual probe.
	[[nodiscard]] inline VulkanDeviceProfileEvaluation EvaluateVulkanAdapterProfilePreliminary(
		const VulkanAdapterCapabilitySnapshot& snapshot) noexcept
	{
		auto capabilities = snapshot.m_ProfileCapabilities;
		capabilities.m_GlobalDescriptorSetLayoutSupported = true;
		return EvaluateVulkanDeviceProfile(capabilities);
	}
}
