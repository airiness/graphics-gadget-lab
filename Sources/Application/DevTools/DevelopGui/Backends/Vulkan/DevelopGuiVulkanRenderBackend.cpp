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

		[[nodiscard]] constexpr bool IsTextureBindingStale(bool publicationMatches,
			uint64_t lastTouchedFrame, uint64_t previousFrame) noexcept
		{
			return !publicationMatches || lastTouchedFrame < previousFrame;
		}

		[[nodiscard]] constexpr bool CanReclaimTextureBinding(bool awaitingSubmission,
			uint64_t lastUseTimeline, uint64_t completedTimeline) noexcept
		{
			return !awaitingSubmission && lastUseTimeline <= completedTimeline;
		}

		static_assert(!IsTextureBindingStale(true, 4, 4));
		static_assert(IsTextureBindingStale(true, 3, 4));
		static_assert(IsTextureBindingStale(false, 4, 4));
		static_assert(!CanReclaimTextureBinding(true, 0, 8));
		static_assert(!CanReclaimTextureBinding(false, 9, 8));
		static_assert(CanReclaimTextureBinding(false, 8, 8));
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
		// The multi-viewport policy stays in io.ConfigFlags (GGLab never enables
		// ImGuiConfigFlags_ViewportsEnable). This capability suppression states
		// a fact about this vendored backend pair, not that policy: the vendored
		// Win32 backend advertises PlatformHasViewports but registers no
		// Platform_CreateVkSurface hook, so with the flag visible
		// ImGui_ImplVulkan_InitMultiViewportSupport() fails its CreateVkSurface
		// assert on every native init and ImGui_ImplVulkan_CreateWindow would
		// call the missing Vk-surface hook. Keep suppressed until a hook exists;
		// do not re-enable it by removing this line.
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
		CompletePreviousFrameTextureUses();
		RetireStaleTextureBindings();
		ReclaimRetiredTextureBindings();
		++m_TextureFrameSerial;
		if (m_TextureFrameSerial == 0)
		{
			m_TextureFrameSerial = 1;
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
		for (size_t index = 0; index < m_ActiveTextureBindings.size();)
		{
			const TextureBinding& binding = m_ActiveTextureBindings[index];
			if (binding.m_SourceDescriptorIndex == descriptor.m_Index &&
				binding.m_Backing.get() != backing.get())
			{
				RetireTextureBinding(index);
				continue;
			}
			++index;
		}
		for (TextureBinding& binding : m_ActiveTextureBindings)
		{
			if (binding.m_Backing.get() == backing.get())
			{
				binding.m_SourceDescriptorIndex = descriptor.m_Index;
				binding.m_LastTouchedFrame = m_TextureFrameSerial;
				binding.m_AwaitingSubmission = true;
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
		m_ActiveTextureBindings.push_back({
			.m_Backing = std::move(backing),
			.m_DescriptorSet = descriptorSet,
			.m_SourceDescriptorIndex = descriptor.m_Index,
			.m_LastTouchedFrame = m_TextureFrameSerial,
			.m_AwaitingSubmission = true,
			});
		return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(descriptorSet));
	}

	void DevelopGuiVulkanRenderBackend::CompletePreviousFrameTextureUses() noexcept
	{
		if (m_TextureFrameSerial == 0 || !m_Context)
		{
			return;
		}
		// ResolveTextureId runs before RHIContext submits the frame. The next
		// successful GUI NewFrame is the first backend callback that can bind
		// those touches to the committed graphics timeline value.
		const uint64_t submittedTimeline = m_Context->GetSubmittedTimelineValue();
		auto completeUses = [this, submittedTimeline](std::vector<TextureBinding>& bindings) noexcept
			{
				for (TextureBinding& binding : bindings)
				{
					if (binding.m_AwaitingSubmission &&
						binding.m_LastTouchedFrame == m_TextureFrameSerial)
					{
						binding.m_LastUseTimelineValue =
							std::max(binding.m_LastUseTimelineValue, submittedTimeline);
						binding.m_AwaitingSubmission = false;
					}
				}
			};
		completeUses(m_ActiveTextureBindings);
		completeUses(m_RetiredTextureBindings);
	}

	void DevelopGuiVulkanRenderBackend::RetireStaleTextureBindings() noexcept
	{
		if (!m_Device)
		{
			return;
		}
		for (size_t index = 0; index < m_ActiveTextureBindings.size();)
		{
			const TextureBinding& binding = m_ActiveTextureBindings[index];
			const auto publishedBacking =
				m_Device->GetDescriptorManager().GetPublishedResourceBacking(
					binding.m_SourceDescriptorIndex);
			// Keep a binding through the immediately following frame so callers
			// resolving it every frame retain a stable ImTextureID. Replacement or
			// one complete untouched frame makes it unreachable by contract.
			if (IsTextureBindingStale(publishedBacking.get() == binding.m_Backing.get(),
				binding.m_LastTouchedFrame, m_TextureFrameSerial))
			{
				RetireTextureBinding(index);
				continue;
			}
			++index;
		}
	}

	void DevelopGuiVulkanRenderBackend::ReclaimRetiredTextureBindings() noexcept
	{
		if (!m_Context || !ImGui::GetCurrentContext() ||
			!ImGui::GetIO().BackendRendererUserData)
		{
			return;
		}
		uint64_t completedTimeline = 0;
		if (!m_Context->TryGetCompletedTimelineValue(completedTimeline))
		{
			return;
		}
		for (size_t index = 0; index < m_RetiredTextureBindings.size();)
		{
			TextureBinding& binding = m_RetiredTextureBindings[index];
			if (!CanReclaimTextureBinding(binding.m_AwaitingSubmission,
				binding.m_LastUseTimelineValue, completedTimeline))
			{
				++index;
				continue;
			}
			if (binding.m_DescriptorSet != VK_NULL_HANDLE)
			{
				ImGui_ImplVulkan_RemoveTexture(binding.m_DescriptorSet);
			}
			m_RetiredTextureBindings.erase(m_RetiredTextureBindings.begin() + index);
		}
	}

	void DevelopGuiVulkanRenderBackend::RetireTextureBinding(size_t index) const noexcept
	{
		GGLAB_ASSERT(index < m_ActiveTextureBindings.size());
		if (index >= m_ActiveTextureBindings.size())
		{
			return;
		}
		m_RetiredTextureBindings.push_back(std::move(m_ActiveTextureBindings[index]));
		m_ActiveTextureBindings.erase(m_ActiveTextureBindings.begin() + index);
	}

	void DevelopGuiVulkanRenderBackend::ReleaseAllTextureBindings() noexcept
	{
		auto release = [](std::vector<TextureBinding>& bindings) noexcept
			{
				for (const TextureBinding& binding : bindings)
				{
					if (binding.m_DescriptorSet != VK_NULL_HANDLE)
					{
						ImGui_ImplVulkan_RemoveTexture(binding.m_DescriptorSet);
					}
				}
				bindings.clear();
			};
		release(m_ActiveTextureBindings);
		release(m_RetiredTextureBindings);
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
		m_PresentationContract = MakePresentationContract(
			colorFormat, swapChain.GetMinImageCount(), imageCount);
		return true;
	}

	void DevelopGuiVulkanRenderBackend::ShutdownNativeBackend() noexcept
	{
		if (ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData)
		{
			ReleaseAllTextureBindings();
			ImGui_ImplVulkan_Shutdown();
		}
		else
		{
			m_ActiveTextureBindings.clear();
			m_RetiredTextureBindings.clear();
		}
		if (m_TextureSampler != VK_NULL_HANDLE && m_Device)
		{
			vkDestroySampler(m_Device->Get(), m_TextureSampler, nullptr);
			m_TextureSampler = VK_NULL_HANDLE;
		}
		m_SwapChainGeneration = 0;
		m_PresentationContract = {};
		m_TextureFrameSerial = 0;
	}

	VulkanPresentationContract DevelopGuiVulkanRenderBackend::MakePresentationContract(
		VkFormat colorFormat, uint32_t minImageCount, uint32_t imageCount) noexcept
	{
		VulkanPresentationContract contract{};
		contract.m_ColorFormat = colorFormat;
		contract.m_ImageCount = imageCount;
		if (imageCount >= 2)
		{
			contract.m_MinImageCount = std::clamp(minImageCount, 2u, imageCount);
		}
		return contract;
	}

	bool DevelopGuiVulkanRenderBackend::IsPresentationContractChanged(
		const VulkanPresentationContract& initializedContract,
		const VulkanPresentationContract& currentContract) noexcept
	{
		// A swapchain that no longer satisfies the renderer's image contract is
		// always a presentation contract change; otherwise the pinned format and
		// image-count bookkeeping are the only contract inputs. Swapchain
		// identity, extent, and generation are deliberately not compared.
		return currentContract.m_ImageCount < 2 ||
			initializedContract != currentContract;
	}

	bool DevelopGuiVulkanRenderBackend::PresentationContractChanged() const noexcept
	{
		const VulkanSwapChain& swapChain = m_Context->GetVulkanSwapChain();
		return IsPresentationContractChanged(
			m_PresentationContract,
			MakePresentationContract(
				swapChain.GetVkFormat(), swapChain.GetMinImageCount(), swapChain.GetImageCount()));
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
