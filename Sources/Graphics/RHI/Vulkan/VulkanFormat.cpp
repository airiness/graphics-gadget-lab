#include "Core/Precompiled.h"
#include "Graphics/RHI/Vulkan/VulkanFormat.h"

#include <array>
#include <format>

namespace gglab
{
	namespace
	{
		inline constexpr std::array<RHIFormat, 2> R8G8B8A8TypelessViewFormats{
			RHIFormat::R8G8B8A8Unorm,
			RHIFormat::R8G8B8A8UnormSrgb,
		};
		inline constexpr std::array<RHIFormat, 1> R16G16B16A16TypelessViewFormats{
			RHIFormat::R16G16B16A16Float,
		};
		inline constexpr std::array<RHIFormat, 2> R32TypelessViewFormats{
			RHIFormat::D32Float,
			RHIFormat::R32Float,
		};

		// One entry per RHIFormat value; the array index is the enum value.
		// Keep in sync with RHIFormatInfos in RHIFormat.h. Formats without a
		// Vulkan equivalent (none today) map to VK_FORMAT_UNDEFINED. An empty
		// m_RHIViewFormats means the format only views as itself.
		inline constexpr std::array<VulkanFormatInfo, static_cast<size_t>(RHIFormat::Count)>
			VulkanFormatInfos = {
			// RHIFormat::Unknown
			VulkanFormatInfo{ RHIFormat::Unknown, VK_FORMAT_UNDEFINED, 0, false, false, {} },
			// RHIFormat::R8G8B8A8Typeless
			VulkanFormatInfo{ RHIFormat::R8G8B8A8Typeless, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, true, false, R8G8B8A8TypelessViewFormats },
			// RHIFormat::R8G8B8A8Unorm
			VulkanFormatInfo{ RHIFormat::R8G8B8A8Unorm, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, false, false, {} },
			// RHIFormat::R8G8B8A8UnormSrgb
			VulkanFormatInfo{ RHIFormat::R8G8B8A8UnormSrgb, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, false, false, {} },
			// RHIFormat::R16G16Float
			VulkanFormatInfo{ RHIFormat::R16G16Float, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, false, false, {} },
			// RHIFormat::R16G16B16A16Typeless
			VulkanFormatInfo{ RHIFormat::R16G16B16A16Typeless, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, true, false, R16G16B16A16TypelessViewFormats },
			// RHIFormat::R16G16B16A16Float
			VulkanFormatInfo{ RHIFormat::R16G16B16A16Float, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, false, false, {} },
			// RHIFormat::R32G32Float
			VulkanFormatInfo{ RHIFormat::R32G32Float, VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, false, false, {} },
			// RHIFormat::R32G32B32Float
			VulkanFormatInfo{ RHIFormat::R32G32B32Float, VK_FORMAT_R32G32B32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, false, false, {} },
			// RHIFormat::R32G32B32A32Float
			VulkanFormatInfo{ RHIFormat::R32G32B32A32Float, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, false, false, {} },
			// RHIFormat::R32Typeless
			VulkanFormatInfo{ RHIFormat::R32Typeless, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, false, true, R32TypelessViewFormats },
			// RHIFormat::R32Float
			VulkanFormatInfo{ RHIFormat::R32Float, VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, false, false, {} },
			// RHIFormat::R32Uint
			VulkanFormatInfo{ RHIFormat::R32Uint, VK_FORMAT_R32_UINT, VK_IMAGE_ASPECT_COLOR_BIT, false, false, {} },
			// RHIFormat::D24UnormS8Uint
			VulkanFormatInfo{ RHIFormat::D24UnormS8Uint, VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, false, true, {} },
			// RHIFormat::D32Float
			VulkanFormatInfo{ RHIFormat::D32Float, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, false, true, {} },
			// RHIFormat::R8Unorm
			VulkanFormatInfo{ RHIFormat::R8Unorm, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, false, false, {} },
			// RHIFormat::R16Float
			VulkanFormatInfo{ RHIFormat::R16Float, VK_FORMAT_R16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, false, false, {} },
			// RHIFormat::B8G8R8A8Unorm
			VulkanFormatInfo{ RHIFormat::B8G8R8A8Unorm, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, false, false, {} },
			// RHIFormat::B8G8R8A8UnormSrgb
			VulkanFormatInfo{ RHIFormat::B8G8R8A8UnormSrgb, VK_FORMAT_B8G8R8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, false, false, {} },
		};
	}

	const VulkanFormatInfo& GetVulkanFormatInfo(RHIFormat format) noexcept
	{
		const size_t index = static_cast<size_t>(format);
		return index < VulkanFormatInfos.size()
			? VulkanFormatInfos[index]
			: VulkanFormatInfos[static_cast<size_t>(RHIFormat::Unknown)];
	}

	VkFormat ToVulkanFormat(RHIFormat format) noexcept
	{
		return GetVulkanFormatInfo(format).m_ResourceFormat;
	}

	VkFormat ToVulkanViewFormat(RHIFormat resourceFormat, RHIFormat viewFormat) noexcept
	{
		const VulkanFormatInfo& resource = GetVulkanFormatInfo(resourceFormat);
		if (resource.m_IsDepthStencil)
		{
			// Depth resources always view through the depth image format:
			// R32Typeless resources interpret sampled views (R32Float) on
			// the depth aspect, never as a color reinterpretation.
			return resource.m_ResourceFormat;
		}
		return ToVulkanFormat(viewFormat);
	}

	bool IsVulkanFormatSupported(RHIFormat format) noexcept
	{
		return ToVulkanFormat(format) != VK_FORMAT_UNDEFINED;
	}

	bool IsVulkanViewFormatCompatible(RHIFormat resourceFormat, RHIFormat viewFormat) noexcept
	{
		const VulkanFormatInfo& resource = GetVulkanFormatInfo(resourceFormat);
		const VulkanFormatInfo& view = GetVulkanFormatInfo(viewFormat);
		if (resource.m_ResourceFormat == VK_FORMAT_UNDEFINED ||
			view.m_ResourceFormat == VK_FORMAT_UNDEFINED)
		{
			return false;
		}
		if (resource.m_RHIViewFormats.empty())
		{
			return resourceFormat == viewFormat;
		}
		for (const RHIFormat candidate : resource.m_RHIViewFormats)
		{
			if (candidate == viewFormat)
			{
				return true;
			}
		}
		return false;
	}

	bool NeedsVulkanMutableFormat(RHIFormat format) noexcept
	{
		const VulkanFormatInfo& info = GetVulkanFormatInfo(format);
		return info.m_IsTypeless && !info.m_IsDepthStencil;
	}

	VkImageUsageFlags ToVulkanImageUsageFlags(RHITextureUsage usage) noexcept
	{
		VkImageUsageFlags flags = 0;
		if (Test(usage, RHITextureUsage::Sampled))
		{
			flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
		}
		if (Test(usage, RHITextureUsage::RenderTarget))
		{
			flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}
		if (Test(usage, RHITextureUsage::DepthStencil))
		{
			flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		}
		if (Test(usage, RHITextureUsage::UnorderedAccess))
		{
			flags |= VK_IMAGE_USAGE_STORAGE_BIT;
		}
		if (Test(usage, RHITextureUsage::CopySource))
		{
			flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		}
		if (Test(usage, RHITextureUsage::CopyDest))
		{
			flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}
		if (Test(usage, RHITextureUsage::Present))
		{
			// Presentation images are created by the WSI layer with a
			// color-attachment usage; the flag contributes nothing here.
			flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}
		return flags;
	}

	VkFormatFeatureFlags2 ToVulkanFormatFeatureFlags(RHITextureUsage usage) noexcept
	{
		VkFormatFeatureFlags2 flags = 0;
		if (Test(usage, RHITextureUsage::Sampled))
		{
			flags |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
		}
		if (Test(usage, RHITextureUsage::RenderTarget))
		{
			flags |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
		}
		if (Test(usage, RHITextureUsage::DepthStencil))
		{
			flags |= VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
		}
		if (Test(usage, RHITextureUsage::UnorderedAccess))
		{
			flags |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT;
		}
		if (Test(usage, RHITextureUsage::CopySource))
		{
			flags |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT;
		}
		if (Test(usage, RHITextureUsage::CopyDest))
		{
			flags |= VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
		}
		return flags;
	}

	VkImageAspectFlags ToVulkanImageAspectFlags(RHITextureAspect aspects) noexcept
	{
		VkImageAspectFlags flags = 0;
		if (Test(aspects, RHITextureAspect::Color))
		{
			flags |= VK_IMAGE_ASPECT_COLOR_BIT;
		}
		if (Test(aspects, RHITextureAspect::Depth))
		{
			flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if (Test(aspects, RHITextureAspect::Stencil))
		{
			flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		return flags;
	}

	VkImageViewType ToVulkanImageViewType(RHITextureViewDimension dimension) noexcept
	{
		switch (dimension)
		{
		case RHITextureViewDimension::Texture1D:
			return VK_IMAGE_VIEW_TYPE_1D;
		case RHITextureViewDimension::Texture1DArray:
			return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
		case RHITextureViewDimension::Texture2D:
			return VK_IMAGE_VIEW_TYPE_2D;
		case RHITextureViewDimension::Texture2DArray:
			return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		case RHITextureViewDimension::Texture3D:
			return VK_IMAGE_VIEW_TYPE_3D;
		case RHITextureViewDimension::TextureCube:
			return VK_IMAGE_VIEW_TYPE_CUBE;
		case RHITextureViewDimension::TextureCubeArray:
			return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
		default:
			return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
		}
	}

	std::optional<VulkanNormalizedTextureView> NormalizeVulkanTextureView(
		const RHITextureDesc& resource, const RHITextureViewDesc& view) noexcept
	{
		// Effective format: Unknown defaults to the resource format.
		const RHIFormat effectiveFormat =
			view.m_Format == RHIFormat::Unknown ? resource.m_Format : view.m_Format;
		if (!IsVulkanFormatSupported(resource.m_Format) ||
			!IsVulkanFormatSupported(effectiveFormat))
		{
			return std::nullopt;
		}

		const VkImageViewType viewType = ToVulkanImageViewType(view.m_Dimension);
		if (viewType == VK_IMAGE_VIEW_TYPE_MAX_ENUM)
		{
			return std::nullopt;
		}

		// Effective resource aspects: depth-stencil formats expose their
		// depth/stencil aspects (R32Typeless included), so a sampled
		// R32Float view on a depth resource resolves to the depth aspect
		// and never to color.
		const RHIFormatInfo& resourceInfo = GetRHIFormatInfo(resource.m_Format);
		const RHITextureAspect effectiveResourceAspects =
			resourceInfo.m_DepthStencilAspects != RHITextureAspect::None
			? resourceInfo.m_DepthStencilAspects
			: resourceInfo.m_Aspects;

		// Aspect: All (the default) resolves to the resource's effective
		// aspects; any other explicit aspect must be a non-empty subset of
		// them, otherwise the view is rejected instead of silently
		// reinterpreted.
		const uint8_t aspectValue = static_cast<uint8_t>(view.m_Subresources.m_Aspects);
		const uint8_t resourceAspectValue = static_cast<uint8_t>(effectiveResourceAspects);
		RHITextureAspect aspect = view.m_Subresources.m_Aspects;
		if (aspect == RHITextureAspect::All)
		{
			aspect = effectiveResourceAspects;
		}
		else if (aspectValue == 0 ||
			(aspectValue & resourceAspectValue) != aspectValue)
		{
			return std::nullopt;
		}

		// Range: base subresources must be in range before Remaining is
		// expanded, so an out-of-range base can never underflow.
		if (view.m_Subresources.m_BaseMip >= resource.m_MipLevels ||
			view.m_Subresources.m_BaseArraySlice >= resource.m_ArraySize)
		{
			return std::nullopt;
		}
		RHISubresourceRange range = view.m_Subresources;
		if (range.m_MipCount == RHISubresourceRange::Remaining)
		{
			range.m_MipCount = resource.m_MipLevels - range.m_BaseMip;
		}
		if (range.m_ArraySliceCount == RHISubresourceRange::Remaining)
		{
			range.m_ArraySliceCount = resource.m_ArraySize - range.m_BaseArraySlice;
		}
		if (range.m_MipCount == 0 || range.m_ArraySliceCount == 0 ||
			range.m_BaseMip + range.m_MipCount > resource.m_MipLevels ||
			range.m_BaseArraySlice + range.m_ArraySliceCount > resource.m_ArraySize)
		{
			return std::nullopt;
		}
		range.m_Aspects = aspect;

		return VulkanNormalizedTextureView{
			.m_EffectiveFormat = effectiveFormat,
			.m_Range = range,
			.m_ViewType = viewType,
			.m_AspectMask = ToVulkanImageAspectFlags(aspect),
			.m_NativeFormat = ToVulkanViewFormat(resource.m_Format, effectiveFormat),
		};
	}

	std::string_view ToVulkanFormatName(VkFormat format) noexcept
	{
		for (const VulkanFormatInfo& info : VulkanFormatInfos)
		{
			if (info.m_ResourceFormat == format)
			{
				return GetRHIFormatInfo(info.m_RHIFormat).m_Name;
			}
		}
		return "VK_FORMAT_UNDEFINED";
	}
}
