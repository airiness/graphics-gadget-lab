#include "DevTools/DevelopGui/Panels/VulkanBackendSummaryPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "Graphics/Renderer.h"
#include "Graphics/RHI/Vulkan/VulkanAdapter.h"
#include "Graphics/RHI/Vulkan/VulkanContext.h"
#include "Graphics/RHI/Vulkan/VulkanDescriptorManager.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanSwapChain.h"

#include <imgui.h>

namespace gglab
{
	namespace
	{
		const char* EnabledText(bool enabled) noexcept
		{
			return enabled ? "enabled" : "disabled";
		}

		void DrawDescriptorSummary(const char* label,
			const VulkanDescriptorPublicationDiagnostics& diagnostics) noexcept
		{
			ImGui::Text("%s: %u live, %u retired, %u free / %u", label,
				diagnostics.m_LiveCount, diagnostics.m_RetiredCount,
				diagnostics.m_FreeCount, diagnostics.m_Capacity);
			ImGui::TextDisabled("  high-water %u, retained backings %u, invalid transitions %llu",
				diagnostics.m_HighWaterMark, diagnostics.m_RetainedBackingCount,
				static_cast<unsigned long long>(diagnostics.m_InvalidTransitionCount));
		}
	}

	void VulkanBackendSummaryPanel::Draw(DevelopGuiContext& context) noexcept
	{
		if (!context.m_Renderer)
		{
			ImGui::TextDisabled("Renderer is unavailable.");
			return;
		}
		auto* vulkanContext =
			dynamic_cast<VulkanContext*>(context.m_Renderer->GetRHIContext());
		if (!vulkanContext)
		{
			ImGui::TextDisabled("The active RHI backend is not Vulkan.");
			return;
		}

		const VulkanAdapterCapabilitySnapshot& snapshot =
			vulkanContext->GetAdapterCapabilitySnapshot();
		const VulkanAdapterIdentity& identity = snapshot.m_Identity;
		const VulkanDeviceProfileCapabilities& capabilities = snapshot.m_ProfileCapabilities;
		const VulkanSwapChain& swapChain = vulkanContext->GetVulkanSwapChain();

		ImGui::SeparatorText("Adapter");
		ImGui::TextUnformatted(identity.m_DeviceName.c_str());
		ImGui::Text("Vulkan %u.%u.%u | vendor 0x%04X | device 0x%04X",
			VK_VERSION_MAJOR(identity.m_ApiVersion), VK_VERSION_MINOR(identity.m_ApiVersion),
			VK_VERSION_PATCH(identity.m_ApiVersion), identity.m_VendorId, identity.m_DeviceId);
		if (!identity.m_DriverName.empty())
		{
			ImGui::Text("Driver: %s", identity.m_DriverName.c_str());
		}
		if (!identity.m_DriverInfo.empty())
		{
			ImGui::TextDisabled("%s", identity.m_DriverInfo.c_str());
		}
		ImGui::Text("Graphics/present queue: family %u, count %u",
			snapshot.m_GraphicsPresentQueueFamilyIndex,
			snapshot.m_GraphicsPresentQueueCount);

		ImGui::SeparatorText("Profile and validation");
		ImGui::Text("Device profile: %s",
			snapshot.m_ProfileEvaluation.IsAccepted() ? "accepted" : "rejected");
		ImGui::Text("Validation: requested %s, messenger %s",
			vulkanContext->IsValidationRequested() ? "yes" : "no",
			EnabledText(vulkanContext->IsValidationEnabled()));
		ImGui::Text("Dynamic rendering: %s | synchronization2: %s | timeline: %s",
			EnabledText(capabilities.m_DynamicRendering),
			EnabledText(capabilities.m_Synchronization2),
			EnabledText(capabilities.m_TimelineSemaphore));
		ImGui::Text("Runtime arrays: %s | partially bound: %s | mutable descriptors: %s",
			EnabledText(capabilities.m_RuntimeDescriptorArray),
			EnabledText(capabilities.m_DescriptorBindingPartiallyBound),
			EnabledText(capabilities.m_MutableDescriptorType));

		ImGui::SeparatorText("Descriptors");
		const VulkanDescriptorManager& descriptorManager =
			vulkanContext->GetVulkanDevice().GetDescriptorManager();
		DrawDescriptorSummary("Resources", descriptorManager.GetResourceDiagnostics());
		DrawDescriptorSummary("Samplers", descriptorManager.GetSamplerDiagnostics());

		ImGui::SeparatorText("Frame transaction");
		ImGui::Text("Swapchain: %ux%u, %u images, VkFormat %d, generation %llu",
			swapChain.GetWidth(), swapChain.GetHeight(), swapChain.GetImageCount(),
			static_cast<int32_t>(swapChain.GetVkFormat()),
			static_cast<unsigned long long>(vulkanContext->GetSwapChainGeneration()));
		uint64_t completedTimeline = 0;
		const bool hasCompletedTimeline =
			vulkanContext->TryGetCompletedTimelineValue(completedTimeline);
		if (hasCompletedTimeline)
		{
			ImGui::Text("Graphics timeline: submitted %llu, completed %llu",
				static_cast<unsigned long long>(vulkanContext->GetSubmittedTimelineValue()),
				static_cast<unsigned long long>(completedTimeline));
		}
		else
		{
			ImGui::Text("Graphics timeline: submitted %llu, completed unavailable",
				static_cast<unsigned long long>(vulkanContext->GetSubmittedTimelineValue()));
		}
		ImGui::Text("Runtime health: %s%s",
			vulkanContext->IsFrameRuntimeFatal() ? "fatal" : "healthy",
			vulkanContext->IsDeviceLost() ? " (device lost)" : "");
	}
}
