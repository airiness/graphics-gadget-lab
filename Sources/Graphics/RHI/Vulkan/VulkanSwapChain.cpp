#include "Core/Precompiled.h"
#include "Graphics/RHI/Vulkan/VulkanSwapChain.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <algorithm>
#include <format>

namespace gglab
{
	namespace
	{
		[[nodiscard]] std::optional<VkSurfaceFormatKHR> SelectSurfaceFormat(
			VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, RHIFormat requestedFormat,
			std::string& outError)
		{
			uint32_t formatCount = 0;
			VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(
				physicalDevice, surface, &formatCount, nullptr);
			if (result != VK_SUCCESS)
			{
				outError = std::format(
					"vkGetPhysicalDeviceSurfaceFormatsKHR failed with {}.", ToString(result));
				return std::nullopt;
			}
			std::vector<VkSurfaceFormatKHR> formats(formatCount);
			if (formatCount > 0)
			{
				result = vkGetPhysicalDeviceSurfaceFormatsKHR(
					physicalDevice, surface, &formatCount, formats.data());
				if (result != VK_SUCCESS)
				{
					outError = std::format(
						"vkGetPhysicalDeviceSurfaceFormatsKHR failed with {}.", ToString(result));
					return std::nullopt;
				}
			}

			const std::optional<VkFormat> requestedVkFormat = ToVulkanSurfaceFormat(requestedFormat);
			if (!requestedVkFormat)
			{
				outError = std::format(
					"Requested swapchain format {:d} is not in the supported WSI format set.",
					static_cast<int>(requestedFormat));
				return std::nullopt;
			}

			// The v1 swapchain contract only accepts VK_COLOR_SPACE_SRGB_NONLINEAR_KHR.
			const auto compatible = [](VkSurfaceFormatKHR candidate) noexcept
				{
					return candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
				};

			// Exact match first.
			for (const VkSurfaceFormatKHR& candidate : formats)
			{
				if (compatible(candidate) && candidate.format == *requestedVkFormat)
				{
					return candidate;
				}
			}

			// Same-color-semantics fallback: UNORM or SRGB families only.
			// UNORM <-> SRGB remapping is never performed silently.
			const bool requestedSrgb =
				requestedFormat == RHIFormat::R8G8B8A8UnormSrgb ||
				requestedFormat == RHIFormat::B8G8R8A8UnormSrgb;
			for (const VkSurfaceFormatKHR& candidate : formats)
			{
				if (!compatible(candidate))
				{
					continue;
				}
				const std::optional<RHIFormat> candidateRhi = FromVulkanSurfaceFormat(candidate.format);
				if (!candidateRhi)
				{
					continue;
				}
				const bool candidateSrgb =
					*candidateRhi == RHIFormat::R8G8B8A8UnormSrgb ||
					*candidateRhi == RHIFormat::B8G8R8A8UnormSrgb;
				if (candidateSrgb == requestedSrgb)
				{
					return candidate;
				}
			}

			outError = "No compatible SDR swapchain format/color space available. Requested: ";
			outError += std::format("RHIFormat {:d} (VkFormat {:d})", static_cast<int>(requestedFormat),
				static_cast<int>(*requestedVkFormat));
			outError += ". Available: ";
			for (const VkSurfaceFormatKHR& candidate : formats)
			{
				outError += std::format("VkFormat {:d}/colorSpace {:d}; ",
					static_cast<int>(candidate.format), static_cast<int>(candidate.colorSpace));
			}
			return std::nullopt;
		}

		[[nodiscard]] std::optional<VkPresentModeKHR> SelectPresentMode(
			VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, bool vsync,
			std::string& outError)
		{
			uint32_t modeCount = 0;
			VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(
				physicalDevice, surface, &modeCount, nullptr);
			if (result != VK_SUCCESS)
			{
				outError = std::format(
					"vkGetPhysicalDeviceSurfacePresentModesKHR failed with {}.", ToString(result));
				return std::nullopt;
			}
			std::vector<VkPresentModeKHR> modes(modeCount);
			if (modeCount > 0)
			{
				result = vkGetPhysicalDeviceSurfacePresentModesKHR(
					physicalDevice, surface, &modeCount, modes.data());
				if (result != VK_SUCCESS)
				{
					outError = std::format(
						"vkGetPhysicalDeviceSurfacePresentModesKHR failed with {}.", ToString(result));
					return std::nullopt;
				}
			}

			const auto has = [&modes](VkPresentModeKHR mode)
				{
					return std::ranges::find(modes, mode) != modes.end();
				};

			const VkPresentModeKHR selected = SelectVulkanPresentModeFromList(modes, vsync);
			if (vsync || has(selected))
			{
				return selected;
			}
			// Unreachable for the documented policy, but keep the error path
			// explicit.
			outError = "No supported present mode is available for the requested VSync policy.";
			return std::nullopt;
		}
	}

	VulkanSwapChain::~VulkanSwapChain()
	{
		Destroy();
	}

	VulkanSwapChain::Result VulkanSwapChain::Create(const VulkanSwapChainCreateInfo& createInfo) noexcept
	{
		Result result{};
		auto swapChain = std::make_unique<VulkanSwapChain>();
		swapChain->m_PhysicalDevice = createInfo.m_PhysicalDevice;
		swapChain->m_Device = createInfo.m_Device;
		swapChain->m_Surface = createInfo.m_Surface;
		swapChain->m_Vsync = createInfo.m_Vsync;

		if (createInfo.m_Width == 0 || createInfo.m_Height == 0)
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			result.m_Error = "VulkanSwapChain requires a non-zero drawable extent.";
			return result;
		}

		const std::optional<VkSurfaceFormatKHR> format = SelectSurfaceFormat(
			createInfo.m_PhysicalDevice, createInfo.m_Surface, createInfo.m_RequestedFormat,
			result.m_Error);
		if (!format)
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			return result;
		}
		swapChain->m_VkFormat = format->format;
		swapChain->m_Format = FromVulkanSurfaceFormat(format->format);

		const std::optional<VkPresentModeKHR> presentMode = SelectPresentMode(
			createInfo.m_PhysicalDevice, createInfo.m_Surface, createInfo.m_Vsync, result.m_Error);
		if (!presentMode)
		{
			result.m_Result = VK_ERROR_INITIALIZATION_FAILED;
			return result;
		}
		swapChain->m_PresentMode = *presentMode;

		VkSurfaceCapabilitiesKHR capabilities{};
		const VkResult capsResult = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
			createInfo.m_PhysicalDevice, createInfo.m_Surface, &capabilities);
		if (capsResult != VK_SUCCESS)
		{
			result.m_Result = capsResult;
			result.m_Error = std::format(
				"vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed with {}.", ToString(capsResult));
			return result;
		}
		swapChain->m_MinImageCount = capabilities.minImageCount;
		swapChain->m_MaxImageCount = capabilities.maxImageCount;

		// Image count policy: min + 1, clamped to the surface maximum.
		uint32_t imageCount = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount > 0)
		{
			imageCount = std::min(imageCount, capabilities.maxImageCount);
		}

		VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		if ((capabilities.supportedCompositeAlpha & compositeAlpha) == 0)
		{
			compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
		}

		VkSwapchainCreateInfoKHR swapChainCreateInfo{};
		swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		swapChainCreateInfo.surface = createInfo.m_Surface;
		swapChainCreateInfo.minImageCount = imageCount;
		swapChainCreateInfo.imageFormat = swapChain->m_VkFormat;
		swapChainCreateInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		swapChainCreateInfo.imageExtent = {
			createInfo.m_Width, createInfo.m_Height,
		};
		swapChainCreateInfo.imageArrayLayers = 1;
		swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swapChainCreateInfo.preTransform = capabilities.currentTransform;
		swapChainCreateInfo.compositeAlpha = compositeAlpha;
		swapChainCreateInfo.presentMode = swapChain->m_PresentMode;
		swapChainCreateInfo.clipped = VK_TRUE;
		swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

		const VkResult createResult = vkCreateSwapchainKHR(
			createInfo.m_Device, &swapChainCreateInfo, nullptr, &swapChain->m_SwapChain);
		if (createResult != VK_SUCCESS)
		{
			result.m_Result = createResult;
			result.m_Error = std::format(
				"vkCreateSwapchainKHR failed with {}.", ToString(createResult));
			return result;
		}
		swapChain->m_Width = createInfo.m_Width;
		swapChain->m_Height = createInfo.m_Height;

		uint32_t imageCountOut = 0;
		VkResult imageResult =
			vkGetSwapchainImagesKHR(createInfo.m_Device, swapChain->m_SwapChain, &imageCountOut, nullptr);
		if (imageResult != VK_SUCCESS)
		{
			result.m_Result = imageResult;
			result.m_Error = std::format(
				"vkGetSwapchainImagesKHR failed with {}.", ToString(imageResult));
			return result;
		}
		std::vector<VkImage> images(imageCountOut);
		imageResult = vkGetSwapchainImagesKHR(
			createInfo.m_Device, swapChain->m_SwapChain, &imageCountOut, images.data());
		if (imageResult != VK_SUCCESS)
		{
			result.m_Result = imageResult;
			result.m_Error = std::format(
				"vkGetSwapchainImagesKHR failed with {}.", ToString(imageResult));
			return result;
		}

		VkImageViewCreateInfo viewCreateInfo{};
		viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCreateInfo.format = swapChain->m_VkFormat;
		viewCreateInfo.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};

		VkSemaphoreCreateInfo semaphoreCreateInfo{};
		semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		swapChain->m_Images.reserve(images.size());
		for (const VkImage image : images)
		{
			VulkanSwapchainImage record{};
			record.m_Image = image;

			viewCreateInfo.image = image;
			const VkResult viewResult =
				vkCreateImageView(createInfo.m_Device, &viewCreateInfo, nullptr, &record.m_ImageView);
			if (viewResult != VK_SUCCESS)
			{
				if (record.m_ImageView != VK_NULL_HANDLE)
				{
					vkDestroyImageView(createInfo.m_Device, record.m_ImageView, nullptr);
				}
				result.m_Result = viewResult;
				result.m_Error = std::format(
					"vkCreateImageView for swapchain image failed with {}.", ToString(viewResult));
				return result;
			}
			const VkResult semaphoreResult = vkCreateSemaphore(
				createInfo.m_Device, &semaphoreCreateInfo, nullptr, &record.m_RenderingFinished);
			if (semaphoreResult != VK_SUCCESS)
			{
				vkDestroyImageView(createInfo.m_Device, record.m_ImageView, nullptr);
				result.m_Result = semaphoreResult;
				result.m_Error = std::format(
					"vkCreateSemaphore for swapchain image failed with {}.", ToString(semaphoreResult));
				return result;
			}
			swapChain->m_Images.push_back(record);
		}

		result.m_SwapChain = std::move(swapChain);
		return result;
	}

	bool VulkanSwapChain::Recreate(
		uint32_t width, uint32_t height, bool vsync, std::string& outError) noexcept
	{
		if (m_Device == VK_NULL_HANDLE || m_Surface == VK_NULL_HANDLE)
		{
			outError = "VulkanSwapChain::Recreate called on an uninitialized swapchain.";
			return false;
		}

		const RHIFormat requested = m_Format;
		DestroyImages();
		vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
		m_SwapChain = VK_NULL_HANDLE;

		VulkanSwapChainCreateInfo createInfo{};
		createInfo.m_PhysicalDevice = m_PhysicalDevice;
		createInfo.m_Device = m_Device;
		createInfo.m_Surface = m_Surface;
		createInfo.m_Width = width;
		createInfo.m_Height = height;
		createInfo.m_RequestedFormat = requested;
		createInfo.m_Vsync = vsync;

		Result result = Create(createInfo);
		if (!result.Succeeded())
		{
			outError = result.m_Error;
			return false;
		}

		// Adopt the fresh swapchain state.
		m_PhysicalDevice = result.m_SwapChain->m_PhysicalDevice;
		m_Device = result.m_SwapChain->m_Device;
		m_Surface = result.m_SwapChain->m_Surface;
		m_SwapChain = result.m_SwapChain->m_SwapChain;
		m_Images = std::move(result.m_SwapChain->m_Images);
		m_Format = result.m_SwapChain->m_Format;
		m_VkFormat = result.m_SwapChain->m_VkFormat;
		m_PresentMode = result.m_SwapChain->m_PresentMode;
		m_Vsync = result.m_SwapChain->m_Vsync;
		m_Width = result.m_SwapChain->m_Width;
		m_Height = result.m_SwapChain->m_Height;
		m_MinImageCount = result.m_SwapChain->m_MinImageCount;
		m_MaxImageCount = result.m_SwapChain->m_MaxImageCount;
		result.m_SwapChain->m_SwapChain = VK_NULL_HANDLE;
		result.m_SwapChain->m_Images.clear();
		return true;
	}

	const VulkanSwapchainImage& VulkanSwapChain::GetImage(uint32_t index) const noexcept
	{
		return m_Images.at(index);
	}

	void VulkanSwapChain::DestroyImages() noexcept
	{
		for (VulkanSwapchainImage& image : m_Images)
		{
			if (image.m_ImageView != VK_NULL_HANDLE)
			{
				vkDestroyImageView(m_Device, image.m_ImageView, nullptr);
				image.m_ImageView = VK_NULL_HANDLE;
			}
			if (image.m_RenderingFinished != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(m_Device, image.m_RenderingFinished, nullptr);
				image.m_RenderingFinished = VK_NULL_HANDLE;
			}
			image.m_Image = VK_NULL_HANDLE;
		}
		m_Images.clear();
	}

	void VulkanSwapChain::Destroy() noexcept
	{
		if (m_Device == VK_NULL_HANDLE)
		{
			return;
		}
		DestroyImages();
		if (m_SwapChain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
			m_SwapChain = VK_NULL_HANDLE;
		}
		m_PhysicalDevice = VK_NULL_HANDLE;
		m_Device = VK_NULL_HANDLE;
		m_Surface = VK_NULL_HANDLE;
	}

	std::optional<VkFormat> ToVulkanSurfaceFormat(RHIFormat format) noexcept
	{
		switch (format)
		{
		case RHIFormat::R8G8B8A8Unorm:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case RHIFormat::R8G8B8A8UnormSrgb:
			return VK_FORMAT_R8G8B8A8_SRGB;
		case RHIFormat::B8G8R8A8Unorm:
			return VK_FORMAT_B8G8R8A8_UNORM;
		case RHIFormat::B8G8R8A8UnormSrgb:
			return VK_FORMAT_B8G8R8A8_SRGB;
		default:
			return std::nullopt;
		}
	}

	RHIFormat FromVulkanSurfaceFormat(VkFormat format) noexcept
	{
		switch (format)
		{
		case VK_FORMAT_R8G8B8A8_UNORM:
			return RHIFormat::R8G8B8A8Unorm;
		case VK_FORMAT_R8G8B8A8_SRGB:
			return RHIFormat::R8G8B8A8UnormSrgb;
		case VK_FORMAT_B8G8R8A8_UNORM:
			return RHIFormat::B8G8R8A8Unorm;
		case VK_FORMAT_B8G8R8A8_SRGB:
			return RHIFormat::B8G8R8A8UnormSrgb;
		default:
			return RHIFormat::Unknown;
		}
	}

	VkPresentModeKHR SelectVulkanPresentModeFromList(
		const std::vector<VkPresentModeKHR>& availableModes, bool vsync) noexcept
	{
		const auto has = [&availableModes](VkPresentModeKHR mode)
			{
				return std::ranges::find(availableModes, mode) != availableModes.end();
			};
		if (vsync)
		{
			// FIFO is guaranteed by the Vulkan spec.
			return VK_PRESENT_MODE_FIFO_KHR;
		}
		if (has(VK_PRESENT_MODE_MAILBOX_KHR))
		{
			return VK_PRESENT_MODE_MAILBOX_KHR;
		}
		if (has(VK_PRESENT_MODE_IMMEDIATE_KHR))
		{
			return VK_PRESENT_MODE_IMMEDIATE_KHR;
		}
		return VK_PRESENT_MODE_FIFO_KHR;
	}
}
