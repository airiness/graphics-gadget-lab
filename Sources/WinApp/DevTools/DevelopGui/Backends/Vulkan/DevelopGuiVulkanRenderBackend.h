#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "DevTools/DevelopGui/DevelopGuiRenderBackend.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace gglab
{
	class VulkanContext;
	class VulkanDescriptorBacking;
	class VulkanDevice;

	// The presentation inputs the ImGui Vulkan renderer actually pins at native
	// initialization: the dynamic-rendering color attachment format and the
	// swapchain image-count bookkeeping it uses to size per-frame state.
	// Swapchain identity, extent, and generation deliberately do not belong to
	// this contract: a swapchain recreation that keeps the format and image
	// count leaves the renderer's pipelines and buffers valid, so it must not
	// force a full renderer restart.
	struct VulkanPresentationContract
	{
		VkFormat m_ColorFormat = VK_FORMAT_UNDEFINED;
		uint32_t m_MinImageCount = 0;
		uint32_t m_ImageCount = 0;

		[[nodiscard]] bool operator==(const VulkanPresentationContract&) const = default;
	};

	class DevelopGuiVulkanRenderBackend final : public DevelopGuiRenderBackend
	{
	public:
		DevelopGuiVulkanRenderBackend() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevelopGuiVulkanRenderBackend);
		~DevelopGuiVulkanRenderBackend() override = default;

		[[nodiscard]] bool Initialize(RHIContext& context) noexcept override;
		void Finalize() noexcept override;
		[[nodiscard]] bool NewFrame() noexcept override;
		void RenderDrawData(RHIGraphicsCommandContext* commandContext,
			RHITextureViewHandle renderTarget) noexcept override;
		[[nodiscard]] ImTextureID ResolveTextureId(
			RHIDescriptorHandle descriptor) const noexcept override;

		[[nodiscard]] static VulkanPresentationContract MakePresentationContract(
			VkFormat colorFormat, uint32_t minImageCount, uint32_t imageCount) noexcept;
		[[nodiscard]] static bool IsPresentationContractChanged(
			const VulkanPresentationContract& initializedContract,
			const VulkanPresentationContract& currentContract) noexcept;

	private:
		struct TextureBinding
		{
			std::shared_ptr<const VulkanDescriptorBacking> m_Backing;
			VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
			uint32_t m_SourceDescriptorIndex = 0;
			uint64_t m_LastTouchedFrame = 0;
			uint64_t m_LastUseTimelineValue = 0;
			bool m_AwaitingSubmission = false;
		};

		[[nodiscard]] bool InitializeNativeBackend() noexcept;
		void ShutdownNativeBackend() noexcept;
		void CompletePreviousFrameTextureUses() noexcept;
		void RetireStaleTextureBindings() noexcept;
		void ReclaimRetiredTextureBindings() noexcept;
		void RetireTextureBinding(size_t index) const noexcept;
		void ReleaseAllTextureBindings() noexcept;
		[[nodiscard]] bool PresentationContractChanged() const noexcept;
		static void CheckVkResult(VkResult result) noexcept;

		VulkanContext* m_Context = nullptr;
		VulkanDevice* m_Device = nullptr;
		VkSampler m_TextureSampler = VK_NULL_HANDLE;
		mutable std::vector<TextureBinding> m_ActiveTextureBindings;
		mutable std::vector<TextureBinding> m_RetiredTextureBindings;
		uint64_t m_TextureFrameSerial = 0;
		// Observability only: the swapchain generation this native renderer was
		// last initialized against. Generation drift on its own is not a
		// presentation contract change.
		uint64_t m_SwapChainGeneration = 0;
		VulkanPresentationContract m_PresentationContract{};
		bool m_IsInitialized = false;
	};
}
