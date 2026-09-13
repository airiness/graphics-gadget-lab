#include "GGLabRuntime/Graphics/RHI/Vulkan/VulkanGuiInterop.h"
#include "Graphics/RHI/Vulkan/VulkanContext.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanSwapChain.h"
#include "Graphics/RHI/Vulkan/VulkanDescriptorManager.h"
#include "Graphics/RHI/Vulkan/VulkanCommandContext.h"

#include <span>

namespace gglab
{
	namespace
	{
		class VulkanGuiInterop final : public VulkanGuiInteropBase
		{
		public:
			explicit VulkanGuiInterop(VulkanContext& context) noexcept : m_Context(context) {}

			VulkanGuiNativeInfo GetNativeInfo() const noexcept override
			{
				auto& device = m_Context.GetVulkanDevice();
				return { device.GetInstance(), device.GetPhysicalDevice(), device.Get(),
					device.GetGraphicsQueue(), device.GetGraphicsQueueFamilyIndex() };
			}

			VulkanGuiPresentationState GetPresentationState() const noexcept override
			{
				const auto& swapChain = m_Context.GetVulkanSwapChain();
				return { swapChain.GetVkFormat(), swapChain.GetMinImageCount(),
					swapChain.GetImageCount(), m_Context.GetSwapChainGeneration() };
			}

			std::shared_ptr<const VulkanImageViewLeaseBase> GetPublishedImage(uint32_t index) const noexcept override
			{
				auto backing = m_Context.GetVulkanDevice().GetDescriptorManager().GetPublishedResourceBacking(index);
				if (!backing || backing->GetKind() != VulkanDescriptorBacking::Kind::ImageView ||
					backing->GetImageView() == VK_NULL_HANDLE) return {};
				return backing;
			}

			VkCommandBuffer PrepareDraw(RHIGraphicsCommandContext* commands,
				RHITextureViewHandle target) noexcept override
			{
				auto* native = dynamic_cast<VulkanGraphicsCommandContext*>(commands);
				GGLAB_ASSERT_NOT_NULL(native);
				if (!native || native->Get() == VK_NULL_HANDLE) return VK_NULL_HANDLE;
				const RHIRenderingAttachment attachment{ .m_View = target };
				commands->BeginRendering({ .m_ColorAttachments =
					std::span<const RHIRenderingAttachment>(&attachment, 1) });
				return native->Get();
			}

			uint64_t GetSubmittedTimelineValue() const noexcept override { return m_Context.GetSubmittedTimelineValue(); }
			bool TryGetCompletedTimelineValue(uint64_t& value) const noexcept override
			{
				return m_Context.TryGetCompletedTimelineValue(value);
			}
			void WaitIdle() noexcept override { m_Context.WaitIdle(); }

		private:
			VulkanContext& m_Context;
		};
	}

	std::unique_ptr<VulkanGuiInteropBase> CreateVulkanGuiInterop(RHIContext& context) noexcept
	{
		auto* native = dynamic_cast<VulkanContext*>(&context);
		return native ? std::make_unique<VulkanGuiInterop>(*native) : nullptr;
	}
}
