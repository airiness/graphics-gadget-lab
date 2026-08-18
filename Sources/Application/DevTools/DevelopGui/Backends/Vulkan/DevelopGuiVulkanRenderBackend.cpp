#include "DevTools/DevelopGui/Backends/Vulkan/DevelopGuiVulkanRenderBackend.h"
#include "Core/Log/LogMacros.h"
#include "Graphics/RHI/RHIContext.h"
#include "Graphics/RHI/Vulkan/VulkanCommandContext.h"
#include "Graphics/RHI/Vulkan/VulkanContext.h"
#include "Graphics/RHI/Vulkan/VulkanDescriptorManager.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanSwapChain.h"

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <format>

namespace gglab
{
	namespace
	{
		constexpr uint32_t DevelopGuiDescriptorPoolSize = 4096;
	}

	bool DevelopGuiVulkanRenderBackend::Initialize(RHIContext& context) noexcept
	{
		if (m_IsInitialized)
		{
			return false;
		}
		m_Context = dynamic_cast<VulkanContext*>(&context);
		if (!m_Context)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DevelopGui Vulkan render backend requires the Vulkan RHI backend.");
			return false;
		}
		m_Device = &m_Context->GetVulkanDevice();
		// GGLab intentionally keeps one application-owned presentation surface.
		// The Win32 backend can advertise platform viewports, but enabling that
		// capability here would hand secondary swapchain ownership to ImGui.
		ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_PlatformHasViewports;
		if (!InitializeNativeBackend())
		{
			m_Context = nullptr;
			m_Device = nullptr;
			return false;
		}
		m_IsInitialized = true;
		return true;
	}

	void DevelopGuiVulkanRenderBackend::Finalize() noexcept
	{
		if (!m_Context && !m_Device)
		{
			return;
		}
		// User texture sets and ImGui pipelines may still be referenced by an
		// in-flight frame. Finalization is an explicit quiescent boundary.
		if (m_IsInitialized && m_Context)
		{
			m_Context->WaitIdle();
		}
		ShutdownNativeBackend();
		m_Context = nullptr;
		m_Device = nullptr;
		m_IsInitialized = false;
	}

	bool DevelopGuiVulkanRenderBackend::NewFrame() noexcept
	{
		GGLAB_ASSERT(m_IsInitialized);
		if (!m_IsInitialized)
		{
			return false;
		}
		if (PresentationContractChanged())
		{
			// VulkanContext recreates only at a queue-idle safe point. Application
			// begins DevelopGui after RHI BeginFrame, so no command in the active
			// transaction has referenced the old ImGui contract yet.
			ShutdownNativeBackend();
			if (!InitializeNativeBackend())
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"Failed to refresh ImGui after Vulkan swapchain recreation.");
				m_IsInitialized = false;
				return false;
			}
		}
		ImGui_ImplVulkan_NewFrame();
		return true;
	}

	void DevelopGuiVulkanRenderBackend::RenderDrawData(
		RHIGraphicsCommandContext* commandContext, RHITextureViewHandle renderTarget) noexcept
	{
		GGLAB_ASSERT(m_IsInitialized);
		if (!m_IsInitialized)
		{
			return;
		}
		auto* vulkanContext = dynamic_cast<VulkanGraphicsCommandContext*>(commandContext);
		GGLAB_ASSERT_NOT_NULL(vulkanContext);
		if (!vulkanContext || vulkanContext->Get() == VK_NULL_HANDLE)
		{
			return;
		}

		const RHIRenderingAttachment colorAttachment{ .m_View = renderTarget };
		commandContext->BeginRendering({ .m_ColorAttachments =
			std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vulkanContext->Get());
	}

	ImTextureID DevelopGuiVulkanRenderBackend::ResolveTextureId(
		RHIDescriptorHandle descriptor) const noexcept
	{
		if (!m_IsInitialized || !m_Device || m_TextureSampler == VK_NULL_HANDLE ||
			!descriptor.IsValid() || descriptor.m_HeapType != RHIDescriptorHeapType::CbvSrvUav)
		{
			return {};
		}
		auto backing = m_Device->GetDescriptorManager().GetPublishedResourceBacking(
			descriptor.m_Index);
		if (!backing || backing->GetKind() != VulkanDescriptorBacking::Kind::ImageView ||
			backing->GetImageView() == VK_NULL_HANDLE)
		{
			return {};
		}
		for (const TextureBinding& binding : m_TextureBindings)
		{
			if (binding.m_Backing.get() == backing.get())
			{
				return static_cast<ImTextureID>(
					reinterpret_cast<uintptr_t>(binding.m_DescriptorSet));
			}
		}

		const VkDescriptorSet descriptorSet = ImGui_ImplVulkan_AddTexture(m_TextureSampler,
			backing->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		if (descriptorSet == VK_NULL_HANDLE)
		{
			GGLAB_LOG_GRAPHICS_ERROR("ImGui failed to allocate a Vulkan texture descriptor.");
			return {};
		}
		m_TextureBindings.push_back({
			.m_Backing = std::move(backing),
			.m_DescriptorSet = descriptorSet,
		});
		return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(descriptorSet));
	}

	bool DevelopGuiVulkanRenderBackend::InitializeNativeBackend() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_Context);
		GGLAB_ASSERT_NOT_NULL(m_Device);
		const VulkanSwapChain& swapChain = m_Context->GetVulkanSwapChain();
		const uint32_t imageCount = swapChain.GetImageCount();
		if (imageCount < 2 || swapChain.GetVkFormat() == VK_FORMAT_UNDEFINED)
		{
			GGLAB_LOG_GRAPHICS_ERROR("Vulkan swapchain does not satisfy the ImGui image contract.");
			return false;
		}
		const uint32_t minImageCount = std::clamp(swapChain.GetMinImageCount(), 2u, imageCount);

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
		const VkResult samplerResult =
			vkCreateSampler(m_Device->Get(), &samplerInfo, nullptr, &m_TextureSampler);
		if (samplerResult != VK_SUCCESS)
		{
			CheckVkResult(samplerResult);
			return false;
		}

		const VkFormat colorFormat = swapChain.GetVkFormat();
		ImGui_ImplVulkan_InitInfo initInfo{};
		initInfo.ApiVersion = VK_API_VERSION_1_3;
		initInfo.Instance = m_Device->GetInstance();
		initInfo.PhysicalDevice = m_Device->GetPhysicalDevice();
		initInfo.Device = m_Device->Get();
		initInfo.QueueFamily = m_Device->GetGraphicsQueueFamilyIndex();
		initInfo.Queue = m_Device->GetGraphicsQueue();
		initInfo.DescriptorPoolSize = DevelopGuiDescriptorPoolSize;
		initInfo.MinImageCount = minImageCount;
		initInfo.ImageCount = imageCount;
		initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
			VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
		initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
		initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
		initInfo.UseDynamicRendering = true;
		initInfo.CheckVkResultFn = &CheckVkResult;
		initInfo.MinAllocationSize = 1024 * 1024;
		if (!ImGui_ImplVulkan_Init(&initInfo))
		{
			GGLAB_LOG_GRAPHICS_ERROR("Failed to initialize the official ImGui Vulkan backend.");
			if (ImGui::GetIO().BackendRendererUserData)
			{
				ImGui_ImplVulkan_Shutdown();
			}
			vkDestroySampler(m_Device->Get(), m_TextureSampler, nullptr);
			m_TextureSampler = VK_NULL_HANDLE;
			return false;
		}

		m_SwapChainGeneration = m_Context->GetSwapChainGeneration();
		m_ColorFormat = colorFormat;
		m_MinImageCount = minImageCount;
		m_ImageCount = imageCount;
		return true;
	}

	void DevelopGuiVulkanRenderBackend::ShutdownNativeBackend() noexcept
	{
		if (ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData)
		{
			for (const TextureBinding& binding : m_TextureBindings)
			{
				if (binding.m_DescriptorSet != VK_NULL_HANDLE)
				{
					ImGui_ImplVulkan_RemoveTexture(binding.m_DescriptorSet);
				}
			}
			m_TextureBindings.clear();
			ImGui_ImplVulkan_Shutdown();
		}
		else
		{
			m_TextureBindings.clear();
		}
		if (m_TextureSampler != VK_NULL_HANDLE && m_Device)
		{
			vkDestroySampler(m_Device->Get(), m_TextureSampler, nullptr);
			m_TextureSampler = VK_NULL_HANDLE;
		}
		m_SwapChainGeneration = 0;
		m_ColorFormat = VK_FORMAT_UNDEFINED;
		m_MinImageCount = 0;
		m_ImageCount = 0;
	}

	bool DevelopGuiVulkanRenderBackend::PresentationContractChanged() const noexcept
	{
		const VulkanSwapChain& swapChain = m_Context->GetVulkanSwapChain();
		const uint32_t imageCount = swapChain.GetImageCount();
		if (imageCount < 2)
		{
			return true;
		}
		return m_SwapChainGeneration != m_Context->GetSwapChainGeneration() ||
			m_ColorFormat != swapChain.GetVkFormat() ||
			m_MinImageCount != std::clamp(swapChain.GetMinImageCount(), 2u, imageCount) ||
			m_ImageCount != imageCount;
	}

	void DevelopGuiVulkanRenderBackend::CheckVkResult(VkResult result) noexcept
	{
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
				"ImGui Vulkan backend call failed with VkResult {}.", static_cast<int32_t>(result)));
		}
	}
}
