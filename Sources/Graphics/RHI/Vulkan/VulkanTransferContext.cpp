#include "Core/Precompiled.h"
#include "Graphics/RHI/RHISubresourceUtils.h"
#include "Graphics/RHI/Vulkan/VulkanBarrier.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanResource.h"
#include "Graphics/RHI/Vulkan/VulkanTextureCopy.h"
#include "Graphics/RHI/Vulkan/VulkanTimelineFence.h"
#include "Graphics/RHI/Vulkan/VulkanTransferContext.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace gglab
{
	VulkanTransferContext::VulkanTransferContext(VulkanDevice* device) noexcept :
		m_Handle(AllocateRHICommandContextHandle()), m_Device(device)
	{
		GGLAB_ASSERT_NOT_NULL(device);
	}

	VulkanTransferContext::~VulkanTransferContext()
	{
		if (m_ExecutingInfo)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTransferContext destroyed while command recording was active; aborting the batch.");
			Abort();
		}
		if (!m_Device || !m_Device->RequireOwnerThread("VulkanTransferContext::~VulkanTransferContext"))
		{
			return;
		}
		VulkanTimelineFence* timeline = m_Device->GetGraphicsTimeline();
		for (const auto& info : m_InFlightInfos)
		{
			if (timeline && info->m_FencePoint.IsValid())
			{
				GGLAB_UNUSED(timeline->Wait(info->m_FencePoint.m_Value));
			}
			DestroyInfo(*info);
		}
		m_InFlightInfos.clear();
		m_Device->RetireCompletedWork();
	}

	bool VulkanTransferContext::CheckRecording(std::string_view operation) noexcept
	{
		if (!m_Device || !m_Device->RequireOwnerThread(operation))
		{
			m_RecordingFailed = true;
			return false;
		}
		if (!m_ExecutingInfo || m_ExecutingInfo->m_CommandBuffer == VK_NULL_HANDLE)
		{
			GGLAB_LOG_GRAPHICS_ERROR("{} requires an active Vulkan transfer batch.", operation);
			m_RecordingFailed = true;
			return false;
		}
		return !m_RecordingFailed;
	}

	void VulkanTransferContext::Begin() noexcept
	{
		if (!m_Device || !m_Device->RequireOwnerThread("VulkanTransferContext::Begin"))
		{
			m_RecordingFailed = true;
			return;
		}
		if (m_ExecutingInfo)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTransferContext::Begin rejected a nested transfer batch.");
			m_RecordingFailed = true;
			return;
		}
		ReclaimCompleted();
		m_RecordingFailed = false;
		m_ExecutingInfo = std::make_unique<InFlightInfo>();

		VkCommandPoolCreateInfo poolCreateInfo{};
		poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		poolCreateInfo.queueFamilyIndex = m_Device->GetGraphicsQueueFamilyIndex();
		VkResult result = vkCreateCommandPool(
			m_Device->Get(), &poolCreateInfo, nullptr, &m_ExecutingInfo->m_CommandPool);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTransferContext failed to create a command pool ({}).", ToString(result));
			m_RecordingFailed = true;
			return;
		}

		VkCommandBufferAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = m_ExecutingInfo->m_CommandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;
		result = vkAllocateCommandBuffers(
			m_Device->Get(), &allocateInfo, &m_ExecutingInfo->m_CommandBuffer);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTransferContext failed to allocate a command buffer ({}).", ToString(result));
			m_RecordingFailed = true;
			return;
		}

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		result = vkBeginCommandBuffer(m_ExecutingInfo->m_CommandBuffer, &beginInfo);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTransferContext failed to begin a command buffer ({}).", ToString(result));
			m_RecordingFailed = true;
		}
	}

	RHIFencePoint VulkanTransferContext::Submit(bool wait) noexcept
	{
		if (!m_Device || !m_Device->RequireOwnerThread("VulkanTransferContext::Submit") ||
			!m_ExecutingInfo)
		{
			return {};
		}
		if (m_RecordingFailed)
		{
			Abort();
			return {};
		}

		const VkResult endResult = vkEndCommandBuffer(m_ExecutingInfo->m_CommandBuffer);
		if (endResult != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTransferContext failed to end a command buffer ({}).", ToString(endResult));
			Abort();
			return {};
		}

		VulkanTimelineFence* timeline = m_Device->GetGraphicsTimeline();
		if (!timeline)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTransferContext requires the graphics timeline owned by VulkanFrameRuntime.");
			Abort();
			return {};
		}
		const uint64_t signalValue = timeline->ReserveSignalValue();
		const VkCommandBufferSubmitInfo commandBufferInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = m_ExecutingInfo->m_CommandBuffer,
		};
		const VkSemaphoreSubmitInfo signalInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = timeline->Get(),
			.value = signalValue,
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		};
		const VkSubmitInfo2 submitInfo{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = &commandBufferInfo,
			.signalSemaphoreInfoCount = 1,
			.pSignalSemaphoreInfos = &signalInfo,
		};
		const VkResult submitResult =
			vkQueueSubmit2(m_Device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
		if (submitResult != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Vulkan transfer queue submission failed with {}.", ToString(submitResult));
			DestroyInfo(*m_ExecutingInfo);
			m_ExecutingInfo.reset();
			return {};
		}

		timeline->CommitSubmittedValue(signalValue);
		const RHIFencePoint fencePoint{ timeline->GetRHIHandle(), signalValue };
		for (const RHIBufferHandle buffer : m_ExecutingInfo->m_UsedBuffers)
		{
			m_Device->RecordBufferUse(buffer, fencePoint);
		}
		for (const RHITextureHandle texture : m_ExecutingInfo->m_UsedTextures)
		{
			m_Device->RecordTextureUse(texture, fencePoint);
		}
		m_ExecutingInfo->m_FencePoint = fencePoint;
		m_InFlightInfos.push_back(std::move(m_ExecutingInfo));
		m_RecordingFailed = false;

		if (wait)
		{
			const VkResult waitResult = timeline->Wait(signalValue);
			if (waitResult != VK_SUCCESS)
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"Vulkan transfer timeline wait failed with {}.", ToString(waitResult));
			}
			ReclaimCompleted();
		}
		return fencePoint;
	}

	void VulkanTransferContext::Abort() noexcept
	{
		if (!m_Device || !m_Device->RequireOwnerThread("VulkanTransferContext::Abort"))
		{
			return;
		}
		if (!m_ExecutingInfo)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTransferContext::Abort called without an active transfer batch.");
			return;
		}
		DestroyInfo(*m_ExecutingInfo);
		m_ExecutingInfo.reset();
		m_RecordingFailed = false;
	}

	void VulkanTransferContext::ReclaimCompleted() noexcept
	{
		if (!m_Device || !m_Device->RequireOwnerThread("VulkanTransferContext::ReclaimCompleted"))
		{
			return;
		}
		for (auto iterator = m_InFlightInfos.begin(); iterator != m_InFlightInfos.end();)
		{
			if (!m_Device->IsFencePointCompleted((*iterator)->m_FencePoint))
			{
				++iterator;
				continue;
			}
			DestroyInfo(**iterator);
			iterator = m_InFlightInfos.erase(iterator);
		}
		m_Device->RetireCompletedWork();
	}

	void VulkanTransferContext::TextureBarrier(
		std::span<const RHITextureBarrier> barriers) noexcept
	{
		if (!CheckRecording("VulkanTransferContext::TextureBarrier") || barriers.empty())
		{
			return;
		}
		std::vector<VkImageMemoryBarrier2> nativeBarriers;
		nativeBarriers.reserve(barriers.size());
		for (const RHITextureBarrier& barrier : barriers)
		{
			if (!IsRHIResourceStateValid(
				barrier.m_Before, RHIResourceStateUsage::TextureBarrierBefore) ||
				!IsRHIResourceStateValid(
					barrier.m_After, RHIResourceStateUsage::TextureBarrierAfter))
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"VulkanTransferContext::TextureBarrier rejected an invalid RHI state.");
				m_RecordingFailed = true;
				return;
			}
			VulkanResourceManager& resources = m_Device->GetResourceManager();
			VulkanTexture* texture = resources.ResolveTexture(barrier.m_Texture);
			const RHITextureDesc* desc = resources.ResolveTextureDesc(barrier.m_Texture);
			if (!texture || !desc)
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"VulkanTransferContext::TextureBarrier received a non-live texture.");
				m_RecordingFailed = true;
				return;
			}
			const RHISubresourceRange range =
				NormalizeTextureSubresourceRange(*desc, barrier.m_Subresources);
			if (range.m_MipCount == 0 || range.m_ArraySliceCount == 0 ||
				range.m_Aspects == RHITextureAspect::None)
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"VulkanTransferContext::TextureBarrier rejected an empty subresource range.");
				m_RecordingFailed = true;
				return;
			}

			VkImageMemoryBarrier2 native{};
			native.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			native.srcStageMask = ToVulkanPipelineStages(barrier.m_Before.m_Stages);
			native.srcAccessMask = ToVulkanAccessFlags(barrier.m_Before.m_Access);
			native.dstStageMask = ToVulkanPipelineStages(barrier.m_After.m_Stages);
			native.dstAccessMask = ToVulkanAccessFlags(barrier.m_After.m_Access);
			native.oldLayout = ToVulkanImageLayout(barrier.m_Before.m_Layout);
			native.newLayout = ToVulkanImageLayout(barrier.m_After.m_Layout);
			native.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			native.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			native.image = texture->Get();
			native.subresourceRange.aspectMask = ToVulkanImageAspectFlags(range.m_Aspects);
			native.subresourceRange.baseMipLevel = range.m_BaseMip;
			native.subresourceRange.levelCount = range.m_MipCount;
			native.subresourceRange.baseArrayLayer = range.m_BaseArraySlice;
			native.subresourceRange.layerCount = range.m_ArraySliceCount;
			nativeBarriers.push_back(native);
			RecordTextureUse(barrier.m_Texture);
		}
		const VkDependencyInfo dependencyInfo{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = static_cast<uint32_t>(nativeBarriers.size()),
			.pImageMemoryBarriers = nativeBarriers.data(),
		};
		vkCmdPipelineBarrier2(m_ExecutingInfo->m_CommandBuffer, &dependencyInfo);
	}

	void VulkanTransferContext::BufferBarrier(
		std::span<const RHIBufferBarrier> barriers) noexcept
	{
		if (!CheckRecording("VulkanTransferContext::BufferBarrier") || barriers.empty())
		{
			return;
		}
		std::vector<VkBufferMemoryBarrier2> nativeBarriers;
		nativeBarriers.reserve(barriers.size());
		for (const RHIBufferBarrier& barrier : barriers)
		{
			if (!IsRHIResourceStateValid(barrier.m_Before, RHIResourceStateUsage::Buffer) ||
				!IsRHIResourceStateValid(barrier.m_After, RHIResourceStateUsage::Buffer))
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"VulkanTransferContext::BufferBarrier rejected an invalid RHI state.");
				m_RecordingFailed = true;
				return;
			}
			VulkanBuffer* buffer = m_Device->GetResourceManager().ResolveBuffer(barrier.m_Buffer);
			if (!buffer)
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"VulkanTransferContext::BufferBarrier received a non-live buffer.");
				m_RecordingFailed = true;
				return;
			}
			VkBufferMemoryBarrier2 native{};
			native.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
			native.srcStageMask = ToVulkanPipelineStages(barrier.m_Before.m_Stages);
			native.srcAccessMask = ToVulkanAccessFlags(barrier.m_Before.m_Access);
			native.dstStageMask = ToVulkanPipelineStages(barrier.m_After.m_Stages);
			native.dstAccessMask = ToVulkanAccessFlags(barrier.m_After.m_Access);
			native.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			native.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			native.buffer = buffer->Get();
			native.offset = 0;
			native.size = VK_WHOLE_SIZE;
			nativeBarriers.push_back(native);
			RecordBufferUse(barrier.m_Buffer);
		}
		const VkDependencyInfo dependencyInfo{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.bufferMemoryBarrierCount = static_cast<uint32_t>(nativeBarriers.size()),
			.pBufferMemoryBarriers = nativeBarriers.data(),
		};
		vkCmdPipelineBarrier2(m_ExecutingInfo->m_CommandBuffer, &dependencyInfo);
	}

	void VulkanTransferContext::CopyBuffer(RHIBufferHandle dst, uint64_t dstOffset,
		RHIBufferHandle src, uint64_t srcOffset, uint64_t numBytes) noexcept
	{
		if (!CheckRecording("VulkanTransferContext::CopyBuffer"))
		{
			return;
		}
		VulkanResourceManager& resources = m_Device->GetResourceManager();
		VulkanBuffer* dstBuffer = resources.ResolveBuffer(dst);
		VulkanBuffer* srcBuffer = resources.ResolveBuffer(src);
		const RHIBufferDesc* dstDesc = resources.ResolveBufferDesc(dst);
		const RHIBufferDesc* srcDesc = resources.ResolveBufferDesc(src);
		if (!dstBuffer || !srcBuffer || !dstDesc || !srcDesc || numBytes == 0 ||
			dstOffset > dstDesc->m_SizeInBytes || numBytes > dstDesc->m_SizeInBytes - dstOffset ||
			srcOffset > srcDesc->m_SizeInBytes || numBytes > srcDesc->m_SizeInBytes - srcOffset ||
			!Test(dstDesc->m_Usage, RHIBufferUsage::CopyDest) ||
			!Test(srcDesc->m_Usage, RHIBufferUsage::CopySource))
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTransferContext::CopyBuffer rejected invalid resources, usage or range.");
			m_RecordingFailed = true;
			return;
		}
		const VkBufferCopy2 region{
			.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
			.srcOffset = srcOffset,
			.dstOffset = dstOffset,
			.size = numBytes,
		};
		const VkCopyBufferInfo2 copyInfo{
			.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
			.srcBuffer = srcBuffer->Get(),
			.dstBuffer = dstBuffer->Get(),
			.regionCount = 1,
			.pRegions = &region,
		};
		vkCmdCopyBuffer2(m_ExecutingInfo->m_CommandBuffer, &copyInfo);
		RecordBufferUse(dst);
		RecordBufferUse(src);
	}

	RHIBufferOwner VulkanTransferContext::CreateStagingBuffer(uint64_t sizeInBytes,
		RHIBufferUsage usage, RHIMemoryUsage memoryUsage, std::string_view owner) noexcept
	{
		RHIBufferDesc desc{};
		desc.m_SizeInBytes = sizeInBytes;
		desc.m_Usage = usage;
		desc.m_MemoryUsage = memoryUsage;
		const RHIResourceDebugIdentityDesc debugIdentity{
			.m_Domain = RHIResourceDebugDomain::Transfer,
			.m_Category = memoryUsage == RHIMemoryUsage::GpuToCpu
				? std::string_view("ReadbackBuffer")
				: std::string_view("UploadBuffer"),
			.m_Label = owner,
			.m_StableId = m_NextDebugOperationSerial++,
		};
		return RHIBufferOwner(m_Device, m_Device->CreateBuffer(desc, debugIdentity));
	}

	bool VulkanTransferContext::UploadBuffer(const void* data, uint64_t sizeInBytes,
		RHIBufferHandle dst, uint64_t dstOffset) noexcept
	{
		if (!CheckRecording("VulkanTransferContext::UploadBuffer"))
		{
			return false;
		}
		VulkanResourceManager& resources = m_Device->GetResourceManager();
		const RHIBufferDesc* dstDesc = resources.ResolveBufferDesc(dst);
		if (!data || !dstDesc || !Test(dstDesc->m_Usage, RHIBufferUsage::CopyDest) ||
			sizeInBytes == 0 || dstOffset > dstDesc->m_SizeInBytes ||
			sizeInBytes > dstDesc->m_SizeInBytes - dstOffset)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTransferContext::UploadBuffer rejected invalid input, usage or range.");
			return false;
		}

		const std::string owner =
			std::format("BufferUpload->RHI={}:{}", dst.Index(), dst.Generation());
		RHIBufferOwner uploadBuffer = CreateStagingBuffer(sizeInBytes,
			RHIBufferUsage::CopySource, RHIMemoryUsage::CpuToGpu, owner);
		if (!uploadBuffer)
		{
			return false;
		}
		void* mapped = m_Device->MapBuffer(uploadBuffer.Get(), {});
		if (!mapped)
		{
			return false;
		}
		std::memcpy(mapped, data, static_cast<size_t>(sizeInBytes));
		m_Device->UnmapBuffer(uploadBuffer.Get(), { 0, sizeInBytes });
		const RHIBufferHandle uploadHandle = uploadBuffer.Get();
		CopyBuffer(dst, dstOffset, uploadHandle, 0, sizeInBytes);
		if (m_RecordingFailed)
		{
			return false;
		}
		m_ExecutingInfo->m_IntermediateBuffers.push_back(std::move(uploadBuffer));
		return true;
	}

	bool VulkanTransferContext::UploadTexture(
		const RHITextureUploadData& uploadData, RHITextureHandle dst) noexcept
	{
		if (!CheckRecording("VulkanTransferContext::UploadTexture"))
		{
			return false;
		}
		VulkanResourceManager& resources = m_Device->GetResourceManager();
		VulkanTexture* texture = resources.ResolveTexture(dst);
		const RHITextureDesc* desc = resources.ResolveTextureDesc(dst);
		if (!texture || !desc || !Test(desc->m_Usage, RHITextureUsage::CopyDest))
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTransferContext::UploadTexture requires a live CopyDest texture.");
			return false;
		}
		const RHITextureValidationResult validation =
			ValidateRHITextureUploadData(*desc, uploadData);
		if (!validation.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTransferContext::UploadTexture rejected the upload: {}.",
				RHITextureValidationErrorText(validation.m_Error));
			return false;
		}
		const auto layout = BuildVulkanTextureCopyLayout(
			*desc, m_Device->GetPhysicalDeviceLimits().optimalBufferCopyOffsetAlignment);
		if (!layout || layout->m_TotalBytes == 0 ||
			layout->m_Subresources.size() != uploadData.m_Subresources.size())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTransferContext::UploadTexture failed to build staging footprints.");
			return false;
		}

		const std::string owner =
			std::format("TextureUpload->RHI={}:{}", dst.Index(), dst.Generation());
		RHIBufferOwner uploadBuffer = CreateStagingBuffer(layout->m_TotalBytes,
			RHIBufferUsage::CopySource, RHIMemoryUsage::CpuToGpu, owner);
		if (!uploadBuffer)
		{
			return false;
		}
		auto* mapped = static_cast<std::byte*>(m_Device->MapBuffer(uploadBuffer.Get(), {}));
		if (!mapped)
		{
			return false;
		}
		for (size_t index = 0; index < layout->m_Subresources.size(); ++index)
		{
			const RHITextureReadbackSubresource& footprint = layout->m_Subresources[index];
			const RHITextureSubresourceData& source = uploadData.m_Subresources[index];
			for (uint32_t depthSlice = 0; depthSlice < footprint.m_Depth; ++depthSlice)
			{
				for (uint32_t row = 0; row < footprint.m_RowCount; ++row)
				{
					const auto* sourceBytes = static_cast<const std::byte*>(source.m_Data) +
						static_cast<uint64_t>(depthSlice) * source.m_SlicePitch +
						static_cast<uint64_t>(row) * source.m_RowPitch;
					std::byte* destination = mapped + footprint.m_BufferOffset +
						static_cast<uint64_t>(depthSlice) * footprint.m_SlicePitch +
						static_cast<uint64_t>(row) * footprint.m_RowPitch;
					std::memcpy(destination, sourceBytes,
						static_cast<size_t>(footprint.m_RowSizeInBytes));
				}
			}
		}
		m_Device->UnmapBuffer(uploadBuffer.Get(), { 0, layout->m_TotalBytes });

		VulkanBuffer* nativeUpload = resources.ResolveBuffer(uploadBuffer.Get());
		GGLAB_ASSERT_NOT_NULL(nativeUpload);
		const VkCopyBufferToImageInfo2 copyInfo{
			.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
			.srcBuffer = nativeUpload->Get(),
			.dstImage = texture->Get(),
			.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.regionCount = static_cast<uint32_t>(layout->m_Regions.size()),
			.pRegions = layout->m_Regions.data(),
		};
		vkCmdCopyBufferToImage2(m_ExecutingInfo->m_CommandBuffer, &copyInfo);
		RecordBufferUse(uploadBuffer.Get());
		RecordTextureUse(dst);
		m_ExecutingInfo->m_IntermediateBuffers.push_back(std::move(uploadBuffer));
		return true;
	}

	RHITextureReadbackRequest VulkanTransferContext::ReadbackTexture(
		RHITextureHandle src, const RHITextureDesc& desc) noexcept
	{
		if (!CheckRecording("VulkanTransferContext::ReadbackTexture"))
		{
			return {};
		}
		VulkanResourceManager& resources = m_Device->GetResourceManager();
		VulkanTexture* texture = resources.ResolveTexture(src);
		const RHITextureDesc* actualDesc = resources.ResolveTextureDesc(src);
		if (!texture || !actualDesc || !Test(actualDesc->m_Usage, RHITextureUsage::CopySource) ||
			actualDesc->m_Dimension != desc.m_Dimension || actualDesc->m_Format != desc.m_Format ||
			actualDesc->m_Extent.m_Width != desc.m_Extent.m_Width ||
			actualDesc->m_Extent.m_Height != desc.m_Extent.m_Height ||
			actualDesc->m_Extent.m_Depth != desc.m_Extent.m_Depth ||
			actualDesc->m_ArraySize != desc.m_ArraySize ||
			actualDesc->m_MipLevels != desc.m_MipLevels || actualDesc->m_SampleCount != desc.m_SampleCount)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanTransferContext::ReadbackTexture rejected a non-live, non-copyable or mismatched texture.");
			return {};
		}
		const auto layout = BuildVulkanTextureCopyLayout(
			desc, m_Device->GetPhysicalDeviceLimits().optimalBufferCopyOffsetAlignment);
		if (!layout || layout->m_TotalBytes == 0)
		{
			return {};
		}

		const std::string owner =
			std::format("TextureReadback<-RHI={}:{}", src.Index(), src.Generation());
		RHITextureReadbackRequest request{};
		request.m_Buffer = CreateStagingBuffer(layout->m_TotalBytes,
			RHIBufferUsage::CopyDest, RHIMemoryUsage::GpuToCpu, owner);
		request.m_BufferSizeInBytes = layout->m_TotalBytes;
		request.m_TextureDesc = desc;
		request.m_TextureDesc.m_DebugName = nullptr;
		request.m_Subresources = layout->m_Subresources;
		if (!request.m_Buffer)
		{
			return {};
		}
		VulkanBuffer* readbackBuffer = resources.ResolveBuffer(request.m_Buffer.Get());
		GGLAB_ASSERT_NOT_NULL(readbackBuffer);
		const VkCopyImageToBufferInfo2 copyInfo{
			.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2,
			.srcImage = texture->Get(),
			.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.dstBuffer = readbackBuffer->Get(),
			.regionCount = static_cast<uint32_t>(layout->m_Regions.size()),
			.pRegions = layout->m_Regions.data(),
		};
		vkCmdCopyImageToBuffer2(m_ExecutingInfo->m_CommandBuffer, &copyInfo);
		RecordTextureUse(src);
		RecordBufferUse(request.m_Buffer.Get());
		return request;
	}

	void VulkanTransferContext::TrackTextureUse(RHITextureHandle texture) noexcept
	{
		if (CheckRecording("VulkanTransferContext::TrackTextureUse"))
		{
			RecordTextureUse(texture);
		}
	}

	void VulkanTransferContext::TrackBufferUse(RHIBufferHandle buffer) noexcept
	{
		if (CheckRecording("VulkanTransferContext::TrackBufferUse"))
		{
			RecordBufferUse(buffer);
		}
	}

	void VulkanTransferContext::RecordBufferUse(RHIBufferHandle buffer) noexcept
	{
		if (!m_ExecutingInfo || !buffer.IsValid())
		{
			return;
		}
		if (std::ranges::find(m_ExecutingInfo->m_UsedBuffers, buffer) ==
			m_ExecutingInfo->m_UsedBuffers.end())
		{
			m_ExecutingInfo->m_UsedBuffers.push_back(buffer);
		}
	}

	void VulkanTransferContext::RecordTextureUse(RHITextureHandle texture) noexcept
	{
		if (!m_ExecutingInfo || !texture.IsValid())
		{
			return;
		}
		if (std::ranges::find(m_ExecutingInfo->m_UsedTextures, texture) ==
			m_ExecutingInfo->m_UsedTextures.end())
		{
			m_ExecutingInfo->m_UsedTextures.push_back(texture);
		}
	}

	void VulkanTransferContext::DestroyInfo(InFlightInfo& info) noexcept
	{
		if (info.m_CommandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(m_Device->Get(), info.m_CommandPool, nullptr);
			info.m_CommandPool = VK_NULL_HANDLE;
			info.m_CommandBuffer = VK_NULL_HANDLE;
		}
		info.m_IntermediateBuffers.clear();
		info.m_UsedBuffers.clear();
		info.m_UsedTextures.clear();
	}
}
