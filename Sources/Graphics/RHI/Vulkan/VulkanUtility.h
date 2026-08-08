#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace gglab
{
	[[nodiscard]] std::string_view ToString(VkResult result) noexcept;

	[[nodiscard]] std::string_view ToString(VkPhysicalDeviceType type) noexcept;

	[[nodiscard]] std::string_view ToString(VkDebugUtilsMessageSeverityFlagBitsEXT severity) noexcept;

	[[nodiscard]] std::string FormatVkVersion(uint32_t version) noexcept;

	// Decodes a Vulkan API version packed with VK_MAKE_API_VERSION.
	[[nodiscard]] uint32_t VkApiVersionMajor(uint32_t version) noexcept;
	[[nodiscard]] uint32_t VkApiVersionMinor(uint32_t version) noexcept;
	[[nodiscard]] uint32_t VkApiVersionPatch(uint32_t version) noexcept;

	[[nodiscard]] bool ContainsExtension(
		std::span<const VkExtensionProperties> extensions, std::string_view name) noexcept;

	[[nodiscard]] bool ContainsLayer(
		std::span<const VkLayerProperties> layers, std::string_view name) noexcept;

	// Formats the physical device API version as "1.3" and the driver version
	// as "0x%08x" (driver encoding is vendor-specific).
	[[nodiscard]] std::string FormatAdapterVersions(
		uint32_t apiVersion, uint32_t driverVersion) noexcept;
}
