#include "Application/SelfTest/DevelopGuiVulkanPresentationContractSelfTests.h"

#include "DevTools/DevelopGui/Backends/Vulkan/DevelopGuiVulkanRenderBackend.h"

#include <vulkan/vulkan.h>

namespace gglab
{
	void RunDevelopGuiVulkanPresentationContractSelfTests(SelfTestContext& context) noexcept
	{
		const VulkanPresentationContract none{};
		const VulkanPresentationContract threeImageSrgb =
			DevelopGuiVulkanRenderBackend::MakePresentationContract(
				VK_FORMAT_B8G8R8A8_SRGB, 2, 3);

		// A swapchain recreation changes identity, extent, and generation. When
		// the color format and image-count bookkeeping survive unchanged (the
		// common resize case), the presentation contract offers the renderer
		// exactly what it pinned, so the native renderer must not be restarted.
		const VulkanPresentationContract afterResizeRecreation =
			DevelopGuiVulkanRenderBackend::MakePresentationContract(
				VK_FORMAT_B8G8R8A8_SRGB, 2, 3);
		context.Check(
			!DevelopGuiVulkanRenderBackend::IsPresentationContractChanged(
				threeImageSrgb, afterResizeRecreation),
			"swapchain recreation that keeps format and image count is not a presentation contract change");

		// The contract pins the format the dynamic-rendering pipeline was built
		// against; a format change is a contract change.
		const VulkanPresentationContract differentFormat =
			DevelopGuiVulkanRenderBackend::MakePresentationContract(
				VK_FORMAT_B10G11R11_UFLOAT_PACK32, 2, 3);
		context.Check(
			DevelopGuiVulkanRenderBackend::IsPresentationContractChanged(
				threeImageSrgb, differentFormat),
			"color format change is a presentation contract change");

		// The per-frame state ring is sized from the image-count bookkeeping; a
		// count or minimum-count change is a contract change.
		const VulkanPresentationContract fourImages =
			DevelopGuiVulkanRenderBackend::MakePresentationContract(
				VK_FORMAT_B8G8R8A8_SRGB, 2, 4);
		const VulkanPresentationContract largerMinimum =
			DevelopGuiVulkanRenderBackend::MakePresentationContract(
				VK_FORMAT_B8G8R8A8_SRGB, 3, 3);
		context.Check(
			DevelopGuiVulkanRenderBackend::IsPresentationContractChanged(
				threeImageSrgb, fourImages) &&
			DevelopGuiVulkanRenderBackend::IsPresentationContractChanged(
				threeImageSrgb, largerMinimum),
			"image count or minimum image count change is a presentation contract change");

		// Minimum count is normalized into [2, image count] on both sides, so a
		// raw request difference that the clamp erases is not a contract change,
		// while an uninitialized or degenerate contract always reports a change.
		context.Check(
			!DevelopGuiVulkanRenderBackend::IsPresentationContractChanged(
				DevelopGuiVulkanRenderBackend::MakePresentationContract(
					VK_FORMAT_B8G8R8A8_SRGB, 1, 3),
				DevelopGuiVulkanRenderBackend::MakePresentationContract(
					VK_FORMAT_B8G8R8A8_SRGB, 2, 3)) &&
			DevelopGuiVulkanRenderBackend::IsPresentationContractChanged(
				threeImageSrgb, none) &&
			DevelopGuiVulkanRenderBackend::IsPresentationContractChanged(
				none, threeImageSrgb),
			"minimum count clamps normalize identically, and an uninitialized or degenerate contract always reports a change");
	}
}
