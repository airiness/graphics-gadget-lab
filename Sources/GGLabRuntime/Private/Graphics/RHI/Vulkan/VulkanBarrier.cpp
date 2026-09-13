#include "Graphics/RHI/Vulkan/VulkanBarrier.h"
#include "Graphics/RHI/RHISubresourceUtils.h"
#include "Graphics/RHI/Vulkan/VulkanFormat.h"

namespace gglab
{
	VkPipelineStageFlags2 ToVulkanPipelineStages(RHIStage stages) noexcept
	{
		VkPipelineStageFlags2 result = VK_PIPELINE_STAGE_2_NONE;
		if (Test(stages, RHIStage::DrawIndirect))
		{
			result |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
		}
		if (Test(stages, RHIStage::IndexInput))
		{
			result |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
				VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
		}
		if (Test(stages, RHIStage::VertexShader))
		{
			result |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
		}
		if (Test(stages, RHIStage::PixelShader))
		{
			result |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		}
		if (Test(stages, RHIStage::ComputeShader))
		{
			result |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		}
		if (Test(stages, RHIStage::RenderTarget))
		{
			result |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		}
		if (Test(stages, RHIStage::DepthStencil))
		{
			result |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
				VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		}
		if (Test(stages, RHIStage::Copy))
		{
			result |= VK_PIPELINE_STAGE_2_COPY_BIT;
		}
		if (Test(stages, RHIStage::Resolve))
		{
			result |= VK_PIPELINE_STAGE_2_RESOLVE_BIT;
		}
		return result;
	}

	VkAccessFlags2 ToVulkanAccessFlags(RHIAccess access) noexcept
	{
		VkAccessFlags2 result = VK_ACCESS_2_NONE;
		if (Test(access, RHIAccess::Common))
		{
			result |= VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		}
		if (Test(access, RHIAccess::ShaderResource))
		{
			result |= VK_ACCESS_2_SHADER_READ_BIT;
		}
		if (Test(access, RHIAccess::RenderTarget))
		{
			result |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
				VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		}
		if (Test(access, RHIAccess::DepthStencilRead))
		{
			result |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		}
		if (Test(access, RHIAccess::DepthStencilWrite))
		{
			result |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}
		if (Test(access, RHIAccess::UnorderedAccess))
		{
			result |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
				VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
		}
		if (Test(access, RHIAccess::CopySource))
		{
			result |= VK_ACCESS_2_TRANSFER_READ_BIT;
		}
		if (Test(access, RHIAccess::CopyDest))
		{
			result |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
		}
		if (Test(access, RHIAccess::VertexBuffer))
		{
			result |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
		}
		if (Test(access, RHIAccess::IndexBuffer))
		{
			result |= VK_ACCESS_2_INDEX_READ_BIT;
		}
		if (Test(access, RHIAccess::ConstantBuffer))
		{
			result |= VK_ACCESS_2_UNIFORM_READ_BIT;
		}
		if (Test(access, RHIAccess::IndirectArgument))
		{
			result |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
		}
		return result;
	}

	std::optional<VkImageLayout> ToVulkanImageLayout(RHILayout layout) noexcept
	{
		switch (layout)
		{
		case RHILayout::Common:
		case RHILayout::UnorderedAccess:
			return VK_IMAGE_LAYOUT_GENERAL;
		case RHILayout::ShaderResource:
			return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		case RHILayout::RenderTarget:
			return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		case RHILayout::DepthStencilRead:
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		case RHILayout::DepthStencilWrite:
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		case RHILayout::CopySource:
			return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		case RHILayout::CopyDest:
			return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		case RHILayout::Present:
			return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		case RHILayout::Undefined:
			return VK_IMAGE_LAYOUT_UNDEFINED;
		case RHILayout::Unknown:
			break;
		}
		return std::nullopt;
	}

	std::optional<VkImageMemoryBarrier2> BuildVulkanTextureBarrier(
		const RHITextureBarrier& barrier, VkImage image, const RHITextureDesc& desc) noexcept
	{
		if (image == VK_NULL_HANDLE ||
			!IsRHIResourceStateValid(
				barrier.m_Before, RHIResourceStateUsage::TextureBarrierBefore) ||
			!IsRHIResourceStateValid(
				barrier.m_After, RHIResourceStateUsage::TextureBarrierAfter))
		{
			return std::nullopt;
		}

		const std::optional<VkImageLayout> oldLayout =
			ToVulkanImageLayout(barrier.m_Before.m_Layout);
		const std::optional<VkImageLayout> newLayout =
			ToVulkanImageLayout(barrier.m_After.m_Layout);
		const RHISubresourceRange range =
			NormalizeTextureSubresourceRange(desc, barrier.m_Subresources);
		const VkImageAspectFlags aspects = ToVulkanImageAspectFlags(range.m_Aspects);
		if (!oldLayout || !newLayout || range.m_MipCount == 0 ||
			range.m_ArraySliceCount == 0 || aspects == 0)
		{
			return std::nullopt;
		}

		const VkImageSubresourceRange nativeRange{
			.aspectMask = aspects,
			.baseMipLevel = range.m_BaseMip,
			.levelCount = range.m_MipCount,
			.baseArrayLayer = range.m_BaseArraySlice,
			.layerCount = range.m_ArraySliceCount,
		};
		return MakeVulkanImageBarrier(image, nativeRange, *oldLayout, *newLayout,
			ToVulkanPipelineStages(barrier.m_Before.m_Stages),
			ToVulkanAccessFlags(barrier.m_Before.m_Access),
			ToVulkanPipelineStages(barrier.m_After.m_Stages),
			ToVulkanAccessFlags(barrier.m_After.m_Access));
	}

	std::optional<VkBufferMemoryBarrier2> BuildVulkanBufferBarrier(
		const RHIBufferBarrier& barrier, VkBuffer buffer) noexcept
	{
		if (buffer == VK_NULL_HANDLE ||
			!IsRHIResourceStateValid(barrier.m_Before, RHIResourceStateUsage::Buffer) ||
			!IsRHIResourceStateValid(barrier.m_After, RHIResourceStateUsage::Buffer))
		{
			return std::nullopt;
		}

		return VkBufferMemoryBarrier2{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
			.srcStageMask = ToVulkanPipelineStages(barrier.m_Before.m_Stages),
			.srcAccessMask = ToVulkanAccessFlags(barrier.m_Before.m_Access),
			.dstStageMask = ToVulkanPipelineStages(barrier.m_After.m_Stages),
			.dstAccessMask = ToVulkanAccessFlags(barrier.m_After.m_Access),
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = buffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		};
	}

	VkImageMemoryBarrier2 MakeVulkanImageBarrier(VkImage image,
		const VkImageSubresourceRange& subresources, VkImageLayout oldLayout,
		VkImageLayout newLayout,
		VkPipelineStageFlags2 sourceStages, VkAccessFlags2 sourceAccess,
		VkPipelineStageFlags2 destinationStages, VkAccessFlags2 destinationAccess) noexcept
	{
		VkImageMemoryBarrier2 barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcStageMask = sourceStages;
		barrier.srcAccessMask = sourceAccess;
		barrier.dstStageMask = destinationStages;
		barrier.dstAccessMask = destinationAccess;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange = subresources;
		return barrier;
	}

	VkImageMemoryBarrier2 MakeVulkanImageBarrier(VkImage image,
		VkImageAspectFlags aspects, VkImageLayout oldLayout, VkImageLayout newLayout,
		VkPipelineStageFlags2 sourceStages, VkAccessFlags2 sourceAccess,
		VkPipelineStageFlags2 destinationStages, VkAccessFlags2 destinationAccess) noexcept
	{
		const VkImageSubresourceRange subresources{
			.aspectMask = aspects,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};
		return MakeVulkanImageBarrier(image, subresources, oldLayout, newLayout,
			sourceStages, sourceAccess, destinationStages, destinationAccess);
	}
}
