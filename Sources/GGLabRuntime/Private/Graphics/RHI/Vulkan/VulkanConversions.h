#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipeline.h"
#include "Graphics/RHI/Vulkan/VulkanShaderBindingABI.h"

#include <vulkan/vulkan.h>

namespace gglab
{
	[[nodiscard]] VkPrimitiveTopology ToVulkanPrimitiveTopology(
		RHIPrimitiveTopology topology) noexcept;
	[[nodiscard]] VkPolygonMode ToVulkanPolygonMode(RHIFillMode mode) noexcept;
	[[nodiscard]] VkCullModeFlags ToVulkanCullMode(RHICullMode mode) noexcept;
	[[nodiscard]] VkFrontFace ToVulkanFrontFace(bool frontCounterClockwise) noexcept;
	[[nodiscard]] VkCompareOp ToVulkanCompareOp(RHICompareOp op) noexcept;
	[[nodiscard]] VkBlendFactor ToVulkanBlendFactor(RHIBlendFactor factor) noexcept;
	[[nodiscard]] VkBlendOp ToVulkanBlendOp(RHIBlendOp op) noexcept;
	[[nodiscard]] VkColorComponentFlags ToVulkanColorWriteMask(RHIColorWriteMask mask) noexcept;
	[[nodiscard]] VkShaderStageFlags ToVulkanShaderStages(RHIShaderStage stages) noexcept;
	[[nodiscard]] VulkanShaderRegisterClass ToVulkanShaderRegisterClass(
		RHIBindingType type) noexcept;
	[[nodiscard]] VkDescriptorType ToVulkanDescriptorType(RHIBindingType type) noexcept;
}
