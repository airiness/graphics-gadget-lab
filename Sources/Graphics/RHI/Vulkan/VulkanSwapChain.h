#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/RHI/RHITypes.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gglab
{
	// Tracked presentation image layout state. Present is a WSI-special
	// state, not an ordinary pipeline stage.
	enum class VulkanPresentImageLayout : uint8_t
	{
		Undefined,       // new or recreated swapchain image; contents unknown
		Present,         // successfully presented; waiting for the next acquire
		ColorAttachment, // in use by this frame's rendering
	};

	// One swapchain image with its WSI-special infrastructure. The VkImage
	// memory is owned by the presentation engine; GGLab owns the image view
	// and the per-image rendering-finished binary semaphore. The tracked
	// presentation layout for these images has a single authority:
	// VulkanImageLayoutTracker in the frame runtime.
	struct VulkanSwapchainImage
	{
		VkImage m_Image = VK_NULL_HANDLE;
		VkImageView m_ImageView = VK_NULL_HANDLE;
		VkSemaphore m_RenderingFinished = VK_NULL_HANDLE;
	};

	struct VulkanSwapChainCreateInfo
	{
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHIFormat m_RequestedFormat = RHIFormat::R8G8B8A8Unorm;
		bool m_Vsync = false;
	};

	class VulkanSwapChain
	{
	public:
		struct Result
		{
			std::unique_ptr<VulkanSwapChain> m_SwapChain;
			std::string m_Error;
			VkResult m_Result = VK_SUCCESS;

			[[nodiscard]] bool Succeeded() const noexcept { return m_SwapChain != nullptr; }
		};

	public:
		VulkanSwapChain() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanSwapChain);
		~VulkanSwapChain();

		[[nodiscard]] static Result Create(const VulkanSwapChainCreateInfo& createInfo) noexcept;

		// Rebuilds the swapchain after the caller has waited for idle. The
		// old swapchain-dependent objects are destroyed and surface
		// capabilities are re-queried; every image starts Undefined.
		[[nodiscard]] bool Recreate(uint32_t width, uint32_t height, bool vsync,
			std::string& outError) noexcept;

		[[nodiscard]] VkSwapchainKHR Get() const noexcept { return m_SwapChain; }
		[[nodiscard]] uint32_t GetImageCount() const noexcept { return static_cast<uint32_t>(m_Images.size()); }
		[[nodiscard]] uint32_t GetWidth() const noexcept { return m_Width; }
		[[nodiscard]] uint32_t GetHeight() const noexcept { return m_Height; }
		[[nodiscard]] VkExtent2D GetExtent() const noexcept
		{
			return VkExtent2D{ m_Width, m_Height };
		}
		[[nodiscard]] RHIFormat GetFormat() const noexcept { return m_Format; }
		[[nodiscard]] VkFormat GetVkFormat() const noexcept { return m_VkFormat; }
		[[nodiscard]] VkPresentModeKHR GetPresentMode() const noexcept { return m_PresentMode; }
		[[nodiscard]] bool GetVsync() const noexcept { return m_Vsync; }
		[[nodiscard]] const VulkanSwapchainImage& GetImage(uint32_t index) const noexcept;
		[[nodiscard]] VkImageView GetImageView(uint32_t index) const noexcept
		{
			return m_Images.at(index).m_ImageView;
		}
		[[nodiscard]] VkSemaphore GetRenderingFinished(uint32_t index) const noexcept
		{
			return m_Images.at(index).m_RenderingFinished;
		}
		[[nodiscard]] uint32_t GetMinImageCount() const noexcept { return m_MinImageCount; }
		[[nodiscard]] uint32_t GetMaxImageCount() const noexcept { return m_MaxImageCount; }

	private:
		void DestroyImages() noexcept;
		void Destroy() noexcept;

		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
		std::vector<VulkanSwapchainImage> m_Images;
		RHIFormat m_Format = RHIFormat::Unknown;
		VkFormat m_VkFormat = VK_FORMAT_UNDEFINED;
		VkPresentModeKHR m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;
		bool m_Vsync = false;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_MinImageCount = 0;
		uint32_t m_MaxImageCount = 0;
	};

	// Minimal WSI-only format mapping. Only the four SDR swapchain formats
	// are supported; HDR and other color spaces are never silently remapped.
	[[nodiscard]] std::optional<VkFormat> ToVulkanSurfaceFormat(RHIFormat format) noexcept;
	[[nodiscard]] RHIFormat FromVulkanSurfaceFormat(VkFormat format) noexcept;

	// Pure present-mode policy over the surface's available modes: VSync on
	// always selects FIFO; VSync off prefers MAILBOX, then IMMEDIATE, then
	// FIFO. FIFO is guaranteed by the Vulkan spec.
	[[nodiscard]] VkPresentModeKHR SelectVulkanPresentModeFromList(
		const std::vector<VkPresentModeKHR>& availableModes, bool vsync) noexcept;
}
