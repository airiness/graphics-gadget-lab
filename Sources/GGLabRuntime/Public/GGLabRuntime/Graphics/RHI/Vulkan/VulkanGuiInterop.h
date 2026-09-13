#pragma once
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/Vulkan/VulkanImageViewLease.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>

namespace gglab
{
	class RHIContext;
	class RHIGraphicsCommandContext;

	struct VulkanGuiNativeInfo
	{
		VkInstance m_Instance = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		uint32_t m_QueueFamily = 0;
	};

	struct VulkanGuiPresentationState
	{
		VkFormat m_ColorFormat = VK_FORMAT_UNDEFINED;
		uint32_t m_MinImageCount = 0;
		uint32_t m_ImageCount = 0;
		uint64_t m_Generation = 0;
	};

	// Backend-specific native integration, not a neutral RHI escape hatch.
	// Context/device outlive the adapter, native GUI objects and retained images.
	class VulkanGuiInteropBase
	{
	public:
		virtual ~VulkanGuiInteropBase() = default;
		[[nodiscard]] virtual VulkanGuiNativeInfo GetNativeInfo() const noexcept = 0;
		[[nodiscard]] virtual VulkanGuiPresentationState GetPresentationState() const noexcept = 0;
		[[nodiscard]] virtual std::shared_ptr<const VulkanImageViewLeaseBase> GetPublishedImage(
			uint32_t descriptorIndex) const noexcept = 0;
		[[nodiscard]] virtual VkCommandBuffer PrepareDraw(
			RHIGraphicsCommandContext* commands, RHITextureViewHandle target) noexcept = 0;
		[[nodiscard]] virtual uint64_t GetSubmittedTimelineValue() const noexcept = 0;
		[[nodiscard]] virtual bool TryGetCompletedTimelineValue(uint64_t& value) const noexcept = 0;
		virtual void WaitIdle() noexcept = 0;
	};

	[[nodiscard]] std::unique_ptr<VulkanGuiInteropBase> CreateVulkanGuiInterop(RHIContext& context) noexcept;
}
