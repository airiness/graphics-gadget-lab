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

		// View type: Unknown (the RHI default) derives from the resource
		// dimension and array size, mirroring the public validator which
		// accepts it; explicit dimensions map directly. Cube views must be
		// requested explicitly because a resource cannot express them.
		RHITextureViewDimension effectiveDimension = view.m_Dimension;
		VkImageViewType viewType = ToVulkanImageViewType(view.m_Dimension);
		if (view.m_Dimension == RHITextureViewDimension::Unknown)
		{
			switch (resource.m_Dimension)
			{
			case RHITextureDimension::Texture1D:
				effectiveDimension = resource.m_ArraySize > 1
					? RHITextureViewDimension::Texture1DArray
					: RHITextureViewDimension::Texture1D;
				viewType = resource.m_ArraySize > 1
					? VK_IMAGE_VIEW_TYPE_1D_ARRAY
					: VK_IMAGE_VIEW_TYPE_1D;
				break;
			case RHITextureDimension::Texture2D:
				effectiveDimension = resource.m_ArraySize > 1
					? RHITextureViewDimension::Texture2DArray
					: RHITextureViewDimension::Texture2D;
				viewType = resource.m_ArraySize > 1
					? VK_IMAGE_VIEW_TYPE_2D_ARRAY
					: VK_IMAGE_VIEW_TYPE_2D;
				break;
			case RHITextureDimension::Texture3D:
				effectiveDimension = RHITextureViewDimension::Texture3D;
				viewType = VK_IMAGE_VIEW_TYPE_3D;
				break;
			}
		}
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
			.m_EffectiveDimension = effectiveDimension,
			.m_Type = view.m_Type,
			.m_Range = range,
			.m_ViewType = viewType,
			.m_AspectMask = ToVulkanImageAspectFlags(aspect),
			.m_NativeFormat = ToVulkanViewFormat(resource.m_Format, effectiveFormat),
		};
	}

	VkSamplerAddressMode ToVulkanSamplerAddressMode(RHITextureAddressMode mode) noexcept
	{
		switch (mode)
		{
		case RHITextureAddressMode::Wrap:
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case RHITextureAddressMode::Mirror:
			return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		case RHITextureAddressMode::Clamp:
			return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case RHITextureAddressMode::Border:
			return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		case RHITextureAddressMode::MirrorOnce:
			return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
		}
		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	}

	VkImageType ToVulkanImageType(RHITextureDimension dimension) noexcept
	{
		switch (dimension)
		{
		case RHITextureDimension::Texture1D:
			return VK_IMAGE_TYPE_1D;
		case RHITextureDimension::Texture2D:
			return VK_IMAGE_TYPE_2D;
		case RHITextureDimension::Texture3D:
			return VK_IMAGE_TYPE_3D;
		}
		return VK_IMAGE_TYPE_2D;
	}

	VkSampleCountFlagBits ToVulkanSampleCount(uint32_t count) noexcept
	{
		// The public validator only admits power-of-two counts in [1, 64];
		// an invalid count must never be silently downgraded to 1x.
		switch (count)
		{
		case 1:
			return VK_SAMPLE_COUNT_1_BIT;
		case 2:
			return VK_SAMPLE_COUNT_2_BIT;
		case 4:
			return VK_SAMPLE_COUNT_4_BIT;
		case 8:
			return VK_SAMPLE_COUNT_8_BIT;
		case 16:
			return VK_SAMPLE_COUNT_16_BIT;
		case 32:
			return VK_SAMPLE_COUNT_32_BIT;
		case 64:
			return VK_SAMPLE_COUNT_64_BIT;
		default:
			GGLAB_UNREACHABLE("Vulkan sample count conversion received an invalid count.");
			return VK_SAMPLE_COUNT_1_BIT;
		}
	}

	VulkanImageCreationContract BuildVulkanImageCreationContract(
		const RHITextureDesc& desc) noexcept
	{
		VulkanImageCreationContract contract{};
		contract.m_Usage = ToVulkanImageUsageFlags(desc.m_Usage);
		if (Test(desc.m_CreateFlags, RHITextureCreateFlags::CubeCompatible))
		{
			contract.m_CreateFlags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		}
		if (NeedsVulkanMutableFormat(desc.m_Format))
		{
			// Typeless families need mutable-format so the restricted view
			// list can be used; the list is the frozen compatible view
			// family contract of the resource format, not an arbitrary
			// format set.
			contract.m_CreateFlags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
			const VulkanFormatInfo& formatInfo = GetVulkanFormatInfo(desc.m_Format);
			GGLAB_ASSERT_MSG(formatInfo.m_RHIViewFormats.size() <= contract.m_ViewFormats.size(),
				"The Vulkan view family exceeds the native format list capacity.");
			for (const RHIFormat viewFormat : formatInfo.m_RHIViewFormats)
			{
				contract.m_ViewFormats[contract.m_ViewFormatCount++] =
					ToVulkanFormat(viewFormat);
			}
		}
		return contract;
	}

	RHITextureSupportResult QueryVulkanTextureSupport(
		VkPhysicalDevice physicalDevice, const RHITextureDesc& desc) noexcept
	{
		const RHITextureValidationResult validation = ValidateRHITextureDesc(desc);
		if (!validation.IsValid())
		{
			return { .m_ValidationError = validation.m_Error };
		}
		if (physicalDevice == VK_NULL_HANDLE)
		{
			return { .m_Reason = RHITextureSupportReason::DeviceUnavailable };
		}
		const VulkanFormatInfo& formatInfo = GetVulkanFormatInfo(desc.m_Format);
		if (!formatInfo.m_IsTypeless && !IsVulkanFormatSupported(desc.m_Format))
		{
			return { .m_Reason = RHITextureSupportReason::FormatSupportQueryFailed };
		}
		const VulkanImageCreationContract contract =
			BuildVulkanImageCreationContract(desc);

		// Feature check first, per single usage: each requested use must
		// have its required format features on the optimal tiling, so the
		// failure reason names the exact missing capability instead of a
		// priority fallback.
		VkFormatProperties3 formatProperties3{};
		formatProperties3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
		VkFormatProperties2 formatProperties2{};
		formatProperties2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
		formatProperties2.pNext = &formatProperties3;
		vkGetPhysicalDeviceFormatProperties2(
			physicalDevice, formatInfo.m_ResourceFormat, &formatProperties2);
		const VkFormatFeatureFlags2 availableFeatures =
			formatProperties3.optimalTilingFeatures;
		for (const RHITextureUsage usage : { RHITextureUsage::Sampled, RHITextureUsage::RenderTarget,
			RHITextureUsage::DepthStencil, RHITextureUsage::UnorderedAccess,
			RHITextureUsage::CopySource, RHITextureUsage::CopyDest })
		{
			if (!Test(desc.m_Usage, usage))
			{
				continue;
			}
			const VkFormatFeatureFlags2 requiredFeatures = ToVulkanFormatFeatureFlags(usage);
			if ((availableFeatures & requiredFeatures) != requiredFeatures)
			{
				return { .m_Reason = RHITextureSupportReasonForUsage(usage) };
			}
		}

		// The query mirrors creation exactly: the same usage, create flags
		// and restricted view format list, so a supported result is what
		// vmaCreateImage will accept. Every single use is satisfied at
		// this point; a failure here is a combination problem.
		VkImageFormatListCreateInfo formatList{};
		formatList.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
		formatList.viewFormatCount = contract.m_ViewFormatCount;
		formatList.pViewFormats = contract.m_ViewFormats.data();
		VkPhysicalDeviceImageFormatInfo2 imageFormatInfo{};
		imageFormatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
		imageFormatInfo.format = formatInfo.m_ResourceFormat;
		imageFormatInfo.type = ToVulkanImageType(desc.m_Dimension);
		imageFormatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageFormatInfo.usage = contract.m_Usage;
		imageFormatInfo.flags = contract.m_CreateFlags;
		// The query does not filter by sample count; the returned
		// sampleCounts mask is checked instead.
		if (contract.m_ViewFormatCount > 0)
		{
			imageFormatInfo.pNext = &formatList;
		}
		VkImageFormatProperties2 imageFormatProperties{};
		imageFormatProperties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
		const VkResult queryResult = vkGetPhysicalDeviceImageFormatProperties2(
			physicalDevice, &imageFormatInfo, &imageFormatProperties);
		if (queryResult == VK_ERROR_FORMAT_NOT_SUPPORTED)
		{
			return { .m_Reason = RHITextureSupportReason::FormatCombinationUnsupported };
		}
		if (queryResult != VK_SUCCESS)
		{
			return { .m_Reason = RHITextureSupportReason::FormatSupportQueryFailed };
		}

		// Sample count must be inside the returned mask.
		const VkSampleCountFlagBits sampleCount = ToVulkanSampleCount(desc.m_SampleCount);
		if ((imageFormatProperties.imageFormatProperties.sampleCounts & sampleCount) == 0)
		{
			return { .m_Reason = RHITextureSupportReason::MultisamplingUnsupported };
		}

		// Size limits must cover the description: extent, mip levels and
		// array layers are the hard boundaries creation depends on.
		const VkExtent3D& maxExtent = imageFormatProperties.imageFormatProperties.maxExtent;
		if (desc.m_Extent.m_Width > maxExtent.width ||
			desc.m_Extent.m_Height > maxExtent.height ||
			desc.m_Extent.m_Depth > maxExtent.depth ||
			desc.m_MipLevels > imageFormatProperties.imageFormatProperties.maxMipLevels ||
			desc.m_ArraySize > imageFormatProperties.imageFormatProperties.maxArrayLayers)
		{
			return { .m_Reason = RHITextureSupportReason::TextureDimensionUnsupported };
		}

		return { .m_Supported = true };
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
