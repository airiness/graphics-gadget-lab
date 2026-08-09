#pragma once
#include "Graphics/RHI/RHIFormat.h"
#include "Graphics/RHI/RHITexture.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace gglab
{
	// Complete RHI-to-Vulkan format contract for owned resource creation and
	// view-family validation. The mapping table is constexpr and
	// CPU-testable; the runtime never hard-codes a format mapping outside
	// this module.
	struct VulkanFormatInfo
	{
		RHIFormat m_RHIFormat = RHIFormat::Unknown;
		VkFormat m_ResourceFormat = VK_FORMAT_UNDEFINED;
		VkImageAspectFlags m_Aspects = 0;
		// Typeless RHI formats create the resource with
		// VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT and restrict views to
		// m_RHIViewFormats. Depth resources are the exception: R32Typeless
		// creates a depth image (D32_SFLOAT) whose sampled interpretation
		// stays on the depth aspect; it is never reinterpreted as color.
		bool m_IsTypeless = false;
		bool m_IsDepthStencil = false;
		// All RHI view formats a view of this resource may legally use.
		std::span<const RHIFormat> m_RHIViewFormats;
	};

	// Returns the complete format contract for an RHI format. Unknown and
	// out-of-range values return the Unknown contract.
	[[nodiscard]] const VulkanFormatInfo& GetVulkanFormatInfo(RHIFormat format) noexcept;

	// The native resource format used to create an owned image. Returns
	// VK_FORMAT_UNDEFINED for unsupported formats.
	[[nodiscard]] VkFormat ToVulkanFormat(RHIFormat format) noexcept;

	// The native format used when creating a view of a resource. For depth
	// resources this is the depth image format (R32Typeless resources view
	// as D32_SFLOAT through the depth aspect); for all other resources it is
	// the view format's native format.
	[[nodiscard]] VkFormat ToVulkanViewFormat(
		RHIFormat resourceFormat, RHIFormat viewFormat) noexcept;

	// True when the RHI format has a valid Vulkan resource mapping.
	[[nodiscard]] bool IsVulkanFormatSupported(RHIFormat format) noexcept;

	// True when a view format is legal on a resource format: the view must
	// be inside the resource's restricted view family.
	[[nodiscard]] bool IsVulkanViewFormatCompatible(
		RHIFormat resourceFormat, RHIFormat viewFormat) noexcept;

	// True when the resource must be created with
	// VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT (typeless family, not depth).
	[[nodiscard]] bool NeedsVulkanMutableFormat(RHIFormat format) noexcept;

	// RHI texture usage to native image usage flags.
	[[nodiscard]] VkImageUsageFlags ToVulkanImageUsageFlags(RHITextureUsage usage) noexcept;

	// RHI texture usage to the format features the usage requires. Present
	// contributes nothing: WSI presentation is not an ordinary pipeline
	// feature and is validated through surface capabilities instead.
	[[nodiscard]] VkFormatFeatureFlags2 ToVulkanFormatFeatureFlags(RHITextureUsage usage) noexcept;

	// RHI aspect mask to native image aspect flags.
	[[nodiscard]] VkImageAspectFlags ToVulkanImageAspectFlags(RHITextureAspect aspects) noexcept;

	// RHI view dimension to native image view type.
	[[nodiscard]] VkImageViewType ToVulkanImageViewType(RHITextureViewDimension dimension) noexcept;

	// Native format name for diagnostics; falls back to a numeric label.
	[[nodiscard]] std::string_view ToVulkanFormatName(VkFormat format) noexcept;
}
