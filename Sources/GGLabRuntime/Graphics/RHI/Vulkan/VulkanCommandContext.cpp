#include "Graphics/RHI/Vulkan/VulkanCommandContext.h"
#include "Core/Log/LogMacros.h"
#include "Graphics/RHI/Vulkan/VulkanBarrier.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineState.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] VkAttachmentLoadOp ToVulkanAttachmentLoadOp(RHIContentLoadOp op) noexcept
		{
			return op == RHIContentLoadOp::DontCare
				? VK_ATTACHMENT_LOAD_OP_DONT_CARE
				: VK_ATTACHMENT_LOAD_OP_LOAD;
		}

		[[nodiscard]] bool Contains(std::span<const RHIBufferHandle> handles,
			RHIBufferHandle handle) noexcept
		{
			return std::ranges::find(handles, handle) != handles.end();
		}

		[[nodiscard]] bool Contains(std::span<const RHITextureHandle> handles,
			RHITextureHandle handle) noexcept
		{
			return std::ranges::find(handles, handle) != handles.end();
		}

		[[nodiscard]] const VulkanSet0BindingPlan* FindSet0Binding(
			const VulkanBindingLayout& layout, uint32_t parameterIndex) noexcept
		{
			const VulkanBindingLayoutPlan& plan = layout.GetPlan();
			for (uint32_t index = 0; index < plan.m_Set0BindingCount; ++index)
			{
				if (plan.m_Set0Bindings[index].m_LogicalParameterIndex == parameterIndex)
				{
					return &plan.m_Set0Bindings[index];
				}
			}
			return nullptr;
		}

		[[nodiscard]] bool BuildSet0BufferBinding(VulkanDevice& device,
			const VulkanBindingLayout& layout, uint32_t parameterIndex, RHIBufferHandle buffer,
			uint64_t offset, VkDescriptorType descriptorType, RHIBufferUsage requiredUsage,
			VulkanSet0BufferBinding& outBinding) noexcept
		{
			const VulkanSet0BindingPlan* binding = FindSet0Binding(layout, parameterIndex);
			VulkanResourceManager& resources = device.GetResourceManager();
			VulkanBuffer* nativeBuffer = resources.ResolveBuffer(buffer);
			const RHIBufferDesc* desc = resources.ResolveBufferDesc(buffer);
			if (binding == nullptr || binding->m_DescriptorCount != 1 ||
				binding->m_DescriptorType != descriptorType || nativeBuffer == nullptr ||
				desc == nullptr || !Test(desc->m_Usage, requiredUsage) ||
				offset >= desc->m_SizeInBytes)
			{
				return false;
			}

			const VkPhysicalDeviceLimits& limits = device.GetPhysicalDeviceLimits();
			const VkDeviceSize alignment = descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
				? limits.minUniformBufferOffsetAlignment
				: limits.minStorageBufferOffsetAlignment;
			if (alignment != 0 && offset % alignment != 0)
			{
				return false;
			}
			const VkDeviceSize remaining = desc->m_SizeInBytes - offset;
			VkDeviceSize range = remaining;
			if (descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
			{
				range = std::min<VkDeviceSize>(remaining, limits.maxUniformBufferRange);
			}
			else if (remaining > limits.maxStorageBufferRange)
			{
				return false;
			}
			outBinding = {
				.m_LogicalParameterIndex = parameterIndex,
				.m_DescriptorType = descriptorType,
				.m_Buffer = nativeBuffer->Get(),
				.m_Offset = offset,
				.m_Range = range,
			};
			return true;
		}

		template <size_t Size>
		[[nodiscard]] std::vector<VulkanSet0BufferBinding> CollectBufferBindings(
			const std::array<std::optional<VulkanSet0BufferBinding>, Size>& bindings)
		{
			std::vector<VulkanSet0BufferBinding> result;
			result.reserve(Size);
			for (const auto& binding : bindings)
			{
				if (binding)
				{
					result.push_back(*binding);
				}
			}
			return result;
		}
	}

	VulkanGraphicsCommandContext::VulkanGraphicsCommandContext(VulkanDevice* device,
		VulkanPipelineSystem* pipelineSystem, VulkanDynamicUniformBuffer* uniformBuffer,
		VulkanSet0DynamicUniformFrames* set0Frames) noexcept :
		m_Device(device), m_PipelineSystem(pipelineSystem), m_UniformBuffer(uniformBuffer),
		m_Set0Frames(set0Frames)
	{
		GGLAB_ASSERT_NOT_NULL(m_Device);
		GGLAB_ASSERT_NOT_NULL(m_PipelineSystem);
		GGLAB_ASSERT_NOT_NULL(m_UniformBuffer);
		GGLAB_ASSERT_NOT_NULL(m_Set0Frames);
	}

	bool VulkanGraphicsCommandContext::BeginEncoding(
		VkCommandBuffer commandBuffer, uint32_t frameSlotIndex) noexcept
	{
		if (m_CommandBuffer != VK_NULL_HANDLE || commandBuffer == VK_NULL_HANDLE ||
			!m_Device->RequireOwnerThread("VulkanGraphicsCommandContext::BeginEncoding") ||
			!m_UniformBuffer->IsFrameActive(frameSlotIndex) ||
			!m_Set0Frames->IsFrameActive(frameSlotIndex))
		{
			return false;
		}
		m_CommandBuffer = commandBuffer;
		m_FrameSlotIndex = frameSlotIndex;
		m_CurrentPipeline = nullptr;
		m_CurrentBindingLayout = nullptr;
		m_CurrentPipelineDesc.reset();
		m_ActiveRenderingSignature.reset();
		m_ActiveColorAttachments.clear();
		m_ActiveDepthAttachment.reset();
		m_DynamicOffsets.clear();
		m_UsedBuffers.clear();
		m_UsedTextures.clear();
		m_PendingImageBarriers.clear();
		m_PendingBufferBarriers.clear();
		m_FixedBufferBindings.fill(std::nullopt);
		m_FixedDescriptorSet = VK_NULL_HANDLE;
		m_FixedDescriptorsDirty = true;
		m_VertexBindings.fill(std::nullopt);
		m_IndexBufferBinding.reset();
		m_PrimitiveTopology = RHIPrimitiveTopology::Unknown;
		m_RenderExtent = {};
		m_IsRendering = false;
		m_ViewportSet = false;
		m_ScissorSet = false;
		m_HasEncodingError = false;
		if (m_DirectComputeContext)
		{
			m_DirectComputeContext->ResetEncodingState();
		}
		return true;
	}

	bool VulkanGraphicsCommandContext::FinishEncoding() noexcept
	{
		if (m_CommandBuffer == VK_NULL_HANDLE || m_IsRendering)
		{
			if (m_IsRendering)
			{
				Reject("FinishEncoding", "an active rendering scope must be ended first");
			}
			return false;
		}
		const bool succeeded = !m_HasEncodingError;
		m_CommandBuffer = VK_NULL_HANDLE;
		m_CurrentPipeline = nullptr;
		m_CurrentBindingLayout = nullptr;
		m_CurrentPipelineDesc.reset();
		m_ActiveRenderingSignature.reset();
		m_ActiveColorAttachments.clear();
		m_ActiveDepthAttachment.reset();
		return succeeded;
	}

	void VulkanGraphicsCommandContext::AbortEncoding() noexcept
	{
		m_CommandBuffer = VK_NULL_HANDLE;
		m_CurrentPipeline = nullptr;
		m_CurrentBindingLayout = nullptr;
		m_CurrentPipelineDesc.reset();
		m_ActiveRenderingSignature.reset();
		m_ActiveColorAttachments.clear();
		m_ActiveDepthAttachment.reset();
		m_PendingImageBarriers.clear();
		m_PendingBufferBarriers.clear();
		m_IsRendering = false;
		m_HasEncodingError = false;
		if (m_DirectComputeContext)
		{
			m_DirectComputeContext->ResetEncodingState();
		}
	}

	void VulkanGraphicsCommandContext::TrackTextureUse(RHITextureHandle texture) noexcept
	{
		if (texture.IsValid() && !Contains(m_UsedTextures, texture))
		{
			m_UsedTextures.push_back(texture);
		}
	}

	void VulkanGraphicsCommandContext::TrackBufferUse(RHIBufferHandle buffer) noexcept
	{
		if (buffer.IsValid() && !Contains(m_UsedBuffers, buffer))
		{
			m_UsedBuffers.push_back(buffer);
		}
	}

	void VulkanGraphicsCommandContext::TextureBarrier(
		std::span<const RHITextureBarrier> barriers) noexcept
	{
		if (barriers.empty())
		{
			return;
		}
		if (m_CommandBuffer == VK_NULL_HANDLE || m_IsRendering)
		{
			Reject("TextureBarrier", m_IsRendering ? "barriers are invalid inside rendering"
				: "no command buffer is active");
			return;
		}

		std::vector<VkImageMemoryBarrier2> nativeBarriers;
		nativeBarriers.reserve(barriers.size());
		for (const RHITextureBarrier& barrier : barriers)
		{
			VulkanResourceManager& resources = m_Device->GetResourceManager();
			VulkanTexture* texture = resources.ResolveTexture(barrier.m_Texture);
			const RHITextureDesc* desc = resources.ResolveTextureDesc(barrier.m_Texture);
			const auto native = texture && desc
				? BuildVulkanTextureBarrier(barrier, texture->Get(), *desc)
				: std::nullopt;
			if (!native)
			{
				Reject("TextureBarrier", "a barrier state, range or texture is invalid");
				return;
			}
			nativeBarriers.push_back(*native);
		}
		m_PendingImageBarriers.insert(m_PendingImageBarriers.end(),
			nativeBarriers.begin(), nativeBarriers.end());
		for (const RHITextureBarrier& barrier : barriers)
		{
			TrackTextureUse(barrier.m_Texture);
		}
	}

	void VulkanGraphicsCommandContext::BufferBarrier(
		std::span<const RHIBufferBarrier> barriers) noexcept
	{
		if (barriers.empty())
		{
			return;
		}
		if (m_CommandBuffer == VK_NULL_HANDLE || m_IsRendering)
		{
			Reject("BufferBarrier", m_IsRendering ? "barriers are invalid inside rendering"
				: "no command buffer is active");
			return;
		}

		std::vector<VkBufferMemoryBarrier2> nativeBarriers;
		nativeBarriers.reserve(barriers.size());
		for (const RHIBufferBarrier& barrier : barriers)
		{
			VulkanBuffer* buffer = m_Device->GetResourceManager().ResolveBuffer(barrier.m_Buffer);
			const auto native = buffer
				? BuildVulkanBufferBarrier(barrier, buffer->Get())
				: std::nullopt;
			if (!native)
			{
				Reject("BufferBarrier", "a barrier state or buffer is invalid");
				return;
			}
			nativeBarriers.push_back(*native);
		}
		m_PendingBufferBarriers.insert(m_PendingBufferBarriers.end(),
			nativeBarriers.begin(), nativeBarriers.end());
		for (const RHIBufferBarrier& barrier : barriers)
		{
			TrackBufferUse(barrier.m_Buffer);
		}
	}

	void VulkanGraphicsCommandContext::FlushBarriers() noexcept
	{
		if (m_PendingImageBarriers.empty() && m_PendingBufferBarriers.empty())
		{
			return;
		}
		if (m_CommandBuffer == VK_NULL_HANDLE || m_IsRendering)
		{
			Reject("FlushBarriers", m_IsRendering ? "barriers are invalid inside rendering"
				: "no command buffer is active");
			return;
		}
		const VkDependencyInfo dependencyInfo{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.bufferMemoryBarrierCount = static_cast<uint32_t>(m_PendingBufferBarriers.size()),
			.pBufferMemoryBarriers = m_PendingBufferBarriers.data(),
			.imageMemoryBarrierCount = static_cast<uint32_t>(m_PendingImageBarriers.size()),
			.pImageMemoryBarriers = m_PendingImageBarriers.data(),
		};
		vkCmdPipelineBarrier2(m_CommandBuffer, &dependencyInfo);
		m_PendingImageBarriers.clear();
		m_PendingBufferBarriers.clear();
	}

	void VulkanGraphicsCommandContext::CopyBuffer(RHIBufferHandle destination,
		uint64_t destinationOffset, RHIBufferHandle source, uint64_t sourceOffset,
		uint64_t sizeInBytes) noexcept
	{
		if (m_CommandBuffer == VK_NULL_HANDLE || m_IsRendering)
		{
			Reject("CopyBuffer", m_IsRendering ? "copies are invalid inside rendering"
				: "no command buffer is active");
			return;
		}
		VulkanResourceManager& resources = m_Device->GetResourceManager();
		VulkanBuffer* destinationBuffer = resources.ResolveBuffer(destination);
		VulkanBuffer* sourceBuffer = resources.ResolveBuffer(source);
		const RHIBufferDesc* destinationDesc = resources.ResolveBufferDesc(destination);
		const RHIBufferDesc* sourceDesc = resources.ResolveBufferDesc(source);
		if (!destinationBuffer || !sourceBuffer || !destinationDesc || !sourceDesc ||
			sizeInBytes == 0 || destinationOffset > destinationDesc->m_SizeInBytes ||
			sizeInBytes > destinationDesc->m_SizeInBytes - destinationOffset ||
			sourceOffset > sourceDesc->m_SizeInBytes ||
			sizeInBytes > sourceDesc->m_SizeInBytes - sourceOffset ||
			!Test(destinationDesc->m_Usage, RHIBufferUsage::CopyDest) ||
			!Test(sourceDesc->m_Usage, RHIBufferUsage::CopySource))
		{
			Reject("CopyBuffer", "a buffer, usage or copy range is invalid");
			return;
		}
		const bool overlaps = destination == source &&
			destinationOffset < sourceOffset + sizeInBytes &&
			sourceOffset < destinationOffset + sizeInBytes;
		if (overlaps)
		{
			Reject("CopyBuffer", "overlapping regions of the same buffer are invalid");
			return;
		}
		const VkBufferCopy2 region{
			.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
			.srcOffset = sourceOffset,
			.dstOffset = destinationOffset,
			.size = sizeInBytes,
		};
		const VkCopyBufferInfo2 copyInfo{
			.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
			.srcBuffer = sourceBuffer->Get(),
			.dstBuffer = destinationBuffer->Get(),
			.regionCount = 1,
			.pRegions = &region,
		};
		vkCmdCopyBuffer2(m_CommandBuffer, &copyInfo);
		TrackBufferUse(destination);
		TrackBufferUse(source);
	}

	void VulkanGraphicsCommandContext::SetPipeline(RHIPipelineHandle pipeline) noexcept
	{
		if (m_CommandBuffer == VK_NULL_HANDLE)
		{
			Reject("SetPipeline", "no command buffer is active");
			return;
		}
		VulkanPipelineState* pipelineState = nullptr;
		VulkanBindingLayout* bindingLayout = nullptr;
		RHIGraphicsPipelineDesc desc{};
		if (!m_PipelineSystem->ResolveGraphicsPipeline(
			pipeline, pipelineState, bindingLayout, desc))
		{
			Reject("SetPipeline", "the pipeline handle is not a live graphics pipeline");
			return;
		}
		if (bindingLayout != m_CurrentBindingLayout)
		{
			if (!m_DynamicUniformState.Initialize(bindingLayout->GetPlan()))
			{
				Reject("SetPipeline", "the dynamic-uniform binding plan is invalid");
				return;
			}
			m_DynamicOffsets.assign(bindingLayout->GetPlan().m_DynamicOffsetCount, 0);
			m_FixedBufferBindings.fill(std::nullopt);
			m_FixedDescriptorSet = VK_NULL_HANDLE;
			m_FixedDescriptorsDirty = true;
		}
		m_CurrentPipeline = pipelineState;
		m_CurrentBindingLayout = bindingLayout;
		m_CurrentPipelineDesc = desc;
		m_PrimitiveTopology = desc.m_PrimitiveTopology;
		vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineState->Get());
	}

	void VulkanGraphicsCommandContext::SetDescriptorTable(
		const RHIDescriptorTableBinding& binding) noexcept
	{
		if (binding.m_HeapType != RHIDescriptorHeapType::CbvSrvUav &&
			binding.m_HeapType != RHIDescriptorHeapType::Sampler)
		{
			Reject("SetDescriptorTable", "the descriptor heap type is not shader visible");
		}
	}

	void VulkanGraphicsCommandContext::BeginRendering(const RHIRenderingInfo& info) noexcept
	{
		if (m_CommandBuffer == VK_NULL_HANDLE || m_IsRendering)
		{
			Reject("BeginRendering", m_IsRendering ? "nested rendering scopes are invalid"
				: "no command buffer is active");
			return;
		}
		if (info.m_ColorAttachments.empty() && !info.m_DepthAttachment)
		{
			Reject("BeginRendering", "at least one attachment is required");
			return;
		}
		if (info.m_ColorAttachments.size() > RHIGraphicsPipelineDesc::MaxRenderTargets)
		{
			Reject("BeginRendering", "the color attachment count exceeds the RHI limit");
			return;
		}
		m_ActiveColorAttachments.clear();
		m_ActiveDepthAttachment.reset();

		std::array<VkRenderingAttachmentInfo, RHIGraphicsPipelineDesc::MaxRenderTargets>
			colorAttachments{};
		RHIRenderingSignature signature{};
		VkExtent2D extent{};
		uint32_t sampleCount = 0;
		for (uint32_t index = 0; index < info.m_ColorAttachments.size(); ++index)
		{
			VulkanResourceManager::TextureViewBinding binding{};
			if (!m_Device->GetResourceManager().ResolveTextureViewBinding(
				info.m_ColorAttachments[index].m_View, binding) ||
				binding.m_ViewDesc.m_Type != RHITextureViewType::RenderTarget ||
				binding.m_ViewDesc.m_Subresources.m_MipCount != 1 ||
				binding.m_ViewDesc.m_Subresources.m_ArraySliceCount != 1)
			{
				Reject("BeginRendering", "a color attachment view is invalid or not singular");
				return;
			}
			const VkExtent2D attachmentExtent{ binding.m_Extent.m_Width, binding.m_Extent.m_Height };
			if (index == 0)
			{
				extent = attachmentExtent;
				sampleCount = binding.m_TextureDesc.m_SampleCount;
			}
			else if (extent.width != attachmentExtent.width || extent.height != attachmentExtent.height ||
				sampleCount != binding.m_TextureDesc.m_SampleCount)
			{
				Reject("BeginRendering", "attachment extents and sample counts must match");
				return;
			}
			colorAttachments[index] = {
				.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
				.imageView = binding.m_ImageView,
				.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.loadOp = ToVulkanAttachmentLoadOp(info.m_ColorAttachments[index].m_LoadOp),
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			};
			signature.m_ColorFormats[index] = binding.m_ViewDesc.m_Format;
			m_ActiveColorAttachments.push_back(binding);
			TrackTextureUse(binding.m_Texture);
		}
		signature.m_ColorAttachmentCount = static_cast<uint32_t>(info.m_ColorAttachments.size());

		VkRenderingAttachmentInfo depthAttachment{};
		if (info.m_DepthAttachment)
		{
			VulkanResourceManager::TextureViewBinding binding{};
			if (!m_Device->GetResourceManager().ResolveTextureViewBinding(
				info.m_DepthAttachment->m_View, binding) ||
				binding.m_ViewDesc.m_Type != RHITextureViewType::DepthStencil ||
				binding.m_ViewDesc.m_Subresources.m_MipCount != 1 ||
				binding.m_ViewDesc.m_Subresources.m_ArraySliceCount != 1 ||
				!Test(binding.m_ViewDesc.m_Subresources.m_Aspects, RHITextureAspect::Depth))
			{
				Reject("BeginRendering", "the depth attachment view is invalid or not singular");
				m_ActiveColorAttachments.clear();
				return;
			}
			const bool hasStencil = Test(
				binding.m_ViewDesc.m_Subresources.m_Aspects, RHITextureAspect::Stencil);
			if (hasStencil && binding.m_ViewDesc.m_ReadOnlyDepth !=
				binding.m_ViewDesc.m_ReadOnlyStencil)
			{
				Reject("BeginRendering",
					"mixed depth/stencil read-only modes require separate-layout support");
				m_ActiveColorAttachments.clear();
				return;
			}
			const VkExtent2D attachmentExtent{ binding.m_Extent.m_Width, binding.m_Extent.m_Height };
			if (extent.width == 0)
			{
				extent = attachmentExtent;
				sampleCount = binding.m_TextureDesc.m_SampleCount;
			}
			else if (extent.width != attachmentExtent.width || extent.height != attachmentExtent.height ||
				sampleCount != binding.m_TextureDesc.m_SampleCount)
			{
				Reject("BeginRendering", "attachment extents and sample counts must match");
				m_ActiveColorAttachments.clear();
				return;
			}
			const bool readOnly = binding.m_ViewDesc.m_ReadOnlyDepth != 0;
			depthAttachment = {
				.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
				.imageView = binding.m_ImageView,
				.imageLayout = readOnly ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
					: VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				.loadOp = ToVulkanAttachmentLoadOp(info.m_DepthAttachment->m_LoadOp),
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			};
			signature.m_DepthFormat = binding.m_ViewDesc.m_Format;
			m_ActiveDepthAttachment = binding;
			TrackTextureUse(binding.m_Texture);
		}
		signature.m_SampleCount = sampleCount;

		VkRenderingInfo rendering{};
		rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		rendering.renderArea.extent = extent;
		rendering.layerCount = 1;
		rendering.colorAttachmentCount = signature.m_ColorAttachmentCount;
		rendering.pColorAttachments = colorAttachments.data();
		if (info.m_DepthAttachment)
		{
			rendering.pDepthAttachment = &depthAttachment;
			if (Test(m_ActiveDepthAttachment->m_ViewDesc.m_Subresources.m_Aspects,
				RHITextureAspect::Stencil))
			{
				rendering.pStencilAttachment = &depthAttachment;
			}
		}
		vkCmdBeginRendering(m_CommandBuffer, &rendering);
		m_ActiveRenderingSignature = signature;
		m_RenderExtent = extent;
		m_IsRendering = true;
	}

	void VulkanGraphicsCommandContext::EndRendering() noexcept
	{
		if (!m_IsRendering)
		{
			Reject("EndRendering", "no rendering scope is active");
			return;
		}
		vkCmdEndRendering(m_CommandBuffer);
		m_IsRendering = false;
		m_ActiveRenderingSignature.reset();
		m_ActiveColorAttachments.clear();
		m_ActiveDepthAttachment.reset();
		m_RenderExtent = {};
	}

	void VulkanGraphicsCommandContext::ClearColorAttachment(
		uint32_t colorAttachmentIndex, const std::array<float, 4>& color) noexcept
	{
		if (!m_IsRendering || colorAttachmentIndex >= m_ActiveColorAttachments.size())
		{
			Reject("ClearColorAttachment", "the attachment index is not active");
			return;
		}
		VkClearAttachment attachment{};
		attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		attachment.colorAttachment = colorAttachmentIndex;
		std::ranges::copy(color, attachment.clearValue.color.float32);
		const VkClearRect rect{ { { 0, 0 }, m_RenderExtent }, 0, 1 };
		vkCmdClearAttachments(m_CommandBuffer, 1, &attachment, 1, &rect);
	}

	void VulkanGraphicsCommandContext::ClearDepthAttachment(
		float depth, std::optional<uint8_t> stencil) noexcept
	{
		if (!m_IsRendering || !m_ActiveDepthAttachment)
		{
			Reject("ClearDepthAttachment", "no depth attachment is active");
			return;
		}
		const RHITextureViewDesc& viewDesc = m_ActiveDepthAttachment->m_ViewDesc;
		if (viewDesc.m_ReadOnlyDepth != 0 ||
			(stencil && (viewDesc.m_ReadOnlyStencil != 0 ||
				!Test(viewDesc.m_Subresources.m_Aspects, RHITextureAspect::Stencil))))
		{
			Reject("ClearDepthAttachment", "the requested aspects are read-only or unavailable");
			return;
		}
		VkClearAttachment attachment{};
		attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (stencil)
		{
			attachment.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		attachment.clearValue.depthStencil = { depth, stencil.value_or(0) };
		const VkClearRect rect{ { { 0, 0 }, m_RenderExtent }, 0, 1 };
		vkCmdClearAttachments(m_CommandBuffer, 1, &attachment, 1, &rect);
	}

	void VulkanGraphicsCommandContext::SetViewport(const RHIViewport& viewport) noexcept
	{
		if (m_CommandBuffer == VK_NULL_HANDLE || viewport.m_Width <= 0.0f ||
			viewport.m_Height <= 0.0f || viewport.m_MinDepth < 0.0f ||
			viewport.m_MaxDepth > 1.0f || viewport.m_MinDepth > viewport.m_MaxDepth)
		{
			Reject("SetViewport", "the viewport violates the RHI coordinate contract");
			return;
		}
		const VkViewport native{
			.x = viewport.m_X,
			.y = viewport.m_Y,
			.width = viewport.m_Width,
			.height = viewport.m_Height,
			.minDepth = viewport.m_MinDepth,
			.maxDepth = viewport.m_MaxDepth,
		};
		vkCmdSetViewport(m_CommandBuffer, 0, 1, &native);
		m_ViewportSet = true;
	}

	void VulkanGraphicsCommandContext::SetScissorRect(const RHIScissorRect& rect) noexcept
	{
		if (m_CommandBuffer == VK_NULL_HANDLE || rect.m_Left < 0 || rect.m_Top < 0 ||
			rect.m_Right < rect.m_Left || rect.m_Bottom < rect.m_Top)
		{
			Reject("SetScissorRect", "the scissor rectangle is invalid");
			return;
		}
		const VkRect2D native{
			.offset = { rect.m_Left, rect.m_Top },
			.extent = { static_cast<uint32_t>(rect.m_Right - rect.m_Left),
				static_cast<uint32_t>(rect.m_Bottom - rect.m_Top) },
		};
		vkCmdSetScissor(m_CommandBuffer, 0, 1, &native);
		m_ScissorSet = true;
	}

	void VulkanGraphicsCommandContext::SetPrimitiveTopology(RHIPrimitiveTopology topology) noexcept
	{
		if (!m_CurrentPipelineDesc || topology != m_CurrentPipelineDesc->m_PrimitiveTopology)
		{
			Reject("SetPrimitiveTopology", "the topology must match the bound pipeline");
			return;
		}
		m_PrimitiveTopology = topology;
	}

	void VulkanGraphicsCommandContext::SetConstantBuffer(
		uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset) noexcept
	{
		(void)SetBufferDescriptor(parameterIndex, buffer, offset,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, RHIBufferUsage::Constant, "SetConstantBuffer");
	}

	void VulkanGraphicsCommandContext::SetReadOnlyBuffer(
		uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset) noexcept
	{
		(void)SetBufferDescriptor(parameterIndex, buffer, offset,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RHIBufferUsage::Structured, "SetReadOnlyBuffer");
	}

	void VulkanGraphicsCommandContext::SetPushConstants(uint32_t parameterIndex,
		std::span<const uint32_t> values, uint32_t destOffset) noexcept
	{
		if (m_CurrentBindingLayout == nullptr)
		{
			Reject("SetPushConstants", "no pipeline binding layout is active");
			return;
		}
		const VulkanDynamicUniformUpdate update = m_DynamicUniformState.SetPushConstants(
			parameterIndex, values, destOffset, *m_UniformBuffer, m_FrameSlotIndex);
		if (!update.IsValid() || update.m_DynamicOffsetSlot >= m_DynamicOffsets.size())
		{
			Reject("SetPushConstants", "the immutable dynamic-uniform allocation failed");
			return;
		}
		m_DynamicOffsets[update.m_DynamicOffsetSlot] = update.m_Allocation.m_DynamicOffset;
		TrackBufferUse(update.m_Allocation.m_Buffer);
	}

	void VulkanGraphicsCommandContext::SetVertexBuffers(uint32_t startSlot,
		std::span<const RHIVertexBufferBinding> bindings) noexcept
	{
		if (m_CommandBuffer == VK_NULL_HANDLE || bindings.empty() ||
			startSlot + bindings.size() > RHIVertexInputLayoutDesc::MaxVertexBuffers)
		{
			Reject("SetVertexBuffers", "the binding range is invalid");
			return;
		}
		std::array<VkBuffer, RHIVertexInputLayoutDesc::MaxVertexBuffers> buffers{};
		std::array<VkDeviceSize, RHIVertexInputLayoutDesc::MaxVertexBuffers> offsets{};
		for (uint32_t index = 0; index < bindings.size(); ++index)
		{
			const RHIVertexBufferBinding& binding = bindings[index];
			VulkanBuffer* buffer = m_Device->GetResourceManager().ResolveBuffer(binding.m_Buffer);
			const RHIBufferDesc* desc =
				m_Device->GetResourceManager().ResolveBufferDesc(binding.m_Buffer);
			if (buffer == nullptr || desc == nullptr || !Test(desc->m_Usage, RHIBufferUsage::Vertex) ||
				binding.m_Stride == 0 || binding.m_Offset > desc->m_SizeInBytes ||
				binding.m_SizeInBytes == 0 ||
				binding.m_SizeInBytes > desc->m_SizeInBytes - binding.m_Offset)
			{
				Reject("SetVertexBuffers", "a vertex buffer binding is invalid");
				return;
			}
			buffers[index] = buffer->Get();
			offsets[index] = binding.m_Offset;
			TrackBufferUse(binding.m_Buffer);
		}
		vkCmdBindVertexBuffers(m_CommandBuffer, startSlot,
			static_cast<uint32_t>(bindings.size()), buffers.data(), offsets.data());
		for (uint32_t index = 0; index < bindings.size(); ++index)
		{
			m_VertexBindings[startSlot + index] = bindings[index];
		}
	}

	void VulkanGraphicsCommandContext::SetIndexBuffer(const RHIIndexBufferBinding& binding) noexcept
	{
		m_IndexBufferBinding.reset();
		VulkanBuffer* buffer = m_Device->GetResourceManager().ResolveBuffer(binding.m_Buffer);
		const RHIBufferDesc* desc = m_Device->GetResourceManager().ResolveBufferDesc(binding.m_Buffer);
		if (m_CommandBuffer == VK_NULL_HANDLE || buffer == nullptr || desc == nullptr ||
			!Test(desc->m_Usage, RHIBufferUsage::Index) ||
			!IsRHIIndexBufferBindingRangeValid(binding, desc->m_SizeInBytes))
		{
			Reject("SetIndexBuffer", "the index buffer binding is invalid");
			return;
		}
		// Vulkan 1.3 binds from offset to the end of VkBuffer. Preserve the narrower
		// RHI range so DrawIndexed can enforce it before recording a native draw.
		vkCmdBindIndexBuffer(m_CommandBuffer, buffer->Get(), binding.m_Offset, VK_INDEX_TYPE_UINT32);
		m_IndexBufferBinding = binding;
		TrackBufferUse(binding.m_Buffer);
	}

	void VulkanGraphicsCommandContext::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
		uint32_t startIndexLocation, int32_t baseVertexLocation,
		uint32_t startInstanceLocation) noexcept
	{
		if (indexCount == 0 || instanceCount == 0 || !ValidateDraw("DrawIndexed", true))
		{
			return;
		}
		if (!IsRHIIndexBufferDrawRangeValid(
			*m_IndexBufferBinding, indexCount, startIndexLocation))
		{
			Reject("DrawIndexed", "the draw exceeds the bound index buffer range");
			return;
		}
		if (!BindDescriptorSets())
		{
			return;
		}
		vkCmdDrawIndexed(m_CommandBuffer, indexCount, instanceCount, startIndexLocation,
			baseVertexLocation, startInstanceLocation);
	}

	void VulkanGraphicsCommandContext::Draw(uint32_t vertexCount, uint32_t instanceCount,
		uint32_t startVertexLocation, uint32_t startInstanceLocation) noexcept
	{
		if (vertexCount == 0 || instanceCount == 0 || !ValidateDraw("Draw", false) ||
			!BindDescriptorSets())
		{
			return;
		}
		vkCmdDraw(m_CommandBuffer, vertexCount, instanceCount, startVertexLocation,
			startInstanceLocation);
	}

	void VulkanGraphicsCommandContext::Reject(
		std::string_view operation, std::string_view reason) noexcept
	{
		m_HasEncodingError = true;
		GGLAB_LOG_GRAPHICS_ERROR("VulkanGraphicsCommandContext::{} rejected the command: {}.",
			operation, reason);
	}

	bool VulkanGraphicsCommandContext::SetBufferDescriptor(uint32_t parameterIndex,
		RHIBufferHandle buffer, uint64_t offset, VkDescriptorType descriptorType,
		RHIBufferUsage requiredUsage, std::string_view operation) noexcept
	{
		VulkanSet0BufferBinding next{};
		if (m_CurrentBindingLayout == nullptr || parameterIndex >= m_FixedBufferBindings.size() ||
			!BuildSet0BufferBinding(*m_Device, *m_CurrentBindingLayout, parameterIndex,
				buffer, offset, descriptorType, requiredUsage, next))
		{
			if (parameterIndex < m_FixedBufferBindings.size())
			{
				m_FixedBufferBindings[parameterIndex].reset();
				m_FixedDescriptorsDirty = true;
			}
			Reject(operation, "the fixed-buffer binding is invalid for the active layout");
			return false;
		}
		if (m_FixedBufferBindings[parameterIndex] != next)
		{
			m_FixedBufferBindings[parameterIndex] = next;
			m_FixedDescriptorsDirty = true;
		}
		TrackBufferUse(buffer);
		return true;
	}

	bool VulkanGraphicsCommandContext::BindDescriptorSets() noexcept
	{
		if (m_CurrentBindingLayout == nullptr)
		{
			Reject("Draw", "no pipeline binding layout is active");
			return false;
		}
		const std::vector fixedBindings = CollectBufferBindings(m_FixedBufferBindings);
		if (m_FixedDescriptorsDirty)
		{
			m_FixedDescriptorSet = m_Set0Frames->AllocateDescriptorSet(
				m_FrameSlotIndex, *m_CurrentBindingLayout, fixedBindings);
			m_FixedDescriptorsDirty = false;
		}
		const std::array sets{
			m_FixedDescriptorSet,
			m_Device->GetDescriptorManager().GetGlobalSet(),
		};
		if (sets[0] == VK_NULL_HANDLE || sets[1] == VK_NULL_HANDLE)
		{
			Reject("Draw", "the descriptor-set snapshot is unavailable");
			return false;
		}
		vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_CurrentBindingLayout->GetPipelineLayout(), 0, static_cast<uint32_t>(sets.size()),
			sets.data(), static_cast<uint32_t>(m_DynamicOffsets.size()), m_DynamicOffsets.data());
		return true;
	}

	VulkanComputeCommandContext::VulkanComputeCommandContext(
		VulkanGraphicsCommandContext& graphicsContext) noexcept :
		m_GraphicsContext(&graphicsContext)
	{
		GGLAB_ASSERT_MSG(graphicsContext.m_DirectComputeContext == nullptr,
			"A Vulkan graphics encoder may have only one direct-compute view.");
		graphicsContext.m_DirectComputeContext = this;
	}

	VulkanComputeCommandContext::~VulkanComputeCommandContext()
	{
		if (m_GraphicsContext && m_GraphicsContext->m_DirectComputeContext == this)
		{
			m_GraphicsContext->m_DirectComputeContext = nullptr;
		}
	}

	void VulkanComputeCommandContext::ResetEncodingState() noexcept
	{
		m_CurrentPipeline = nullptr;
		m_CurrentBindingLayout = nullptr;
		m_DynamicOffsets.clear();
		m_FixedBufferBindings.fill(std::nullopt);
		m_FixedDescriptorSet = VK_NULL_HANDLE;
		m_FixedDescriptorsDirty = true;
	}

	void VulkanComputeCommandContext::TrackTextureUse(RHITextureHandle texture) noexcept
	{
		m_GraphicsContext->TrackTextureUse(texture);
	}

	void VulkanComputeCommandContext::TrackBufferUse(RHIBufferHandle buffer) noexcept
	{
		m_GraphicsContext->TrackBufferUse(buffer);
	}

	void VulkanComputeCommandContext::TextureBarrier(
		std::span<const RHITextureBarrier> barriers) noexcept
	{
		m_GraphicsContext->TextureBarrier(barriers);
	}

	void VulkanComputeCommandContext::BufferBarrier(
		std::span<const RHIBufferBarrier> barriers) noexcept
	{
		m_GraphicsContext->BufferBarrier(barriers);
	}

	void VulkanComputeCommandContext::FlushBarriers() noexcept
	{
		m_GraphicsContext->FlushBarriers();
	}

	void VulkanComputeCommandContext::CopyBuffer(RHIBufferHandle destination,
		uint64_t destinationOffset, RHIBufferHandle source, uint64_t sourceOffset,
		uint64_t sizeInBytes) noexcept
	{
		m_GraphicsContext->CopyBuffer(
			destination, destinationOffset, source, sourceOffset, sizeInBytes);
	}

	void VulkanComputeCommandContext::SetPipeline(RHIPipelineHandle pipeline) noexcept
	{
		if (m_GraphicsContext->m_CommandBuffer == VK_NULL_HANDLE)
		{
			Reject("SetPipeline", "no command buffer is active");
			return;
		}
		VulkanPipelineState* pipelineState = nullptr;
		VulkanBindingLayout* bindingLayout = nullptr;
		if (!m_GraphicsContext->m_PipelineSystem->ResolveComputePipeline(
			pipeline, pipelineState, bindingLayout))
		{
			Reject("SetPipeline", "the pipeline or its active set-0 layout is invalid");
			return;
		}
		if (bindingLayout != m_CurrentBindingLayout)
		{
			if (!m_DynamicUniformState.Initialize(bindingLayout->GetPlan()))
			{
				Reject("SetPipeline", "the dynamic-uniform binding plan is invalid");
				return;
			}
			m_DynamicOffsets.assign(bindingLayout->GetPlan().m_DynamicOffsetCount, 0);
			m_FixedBufferBindings.fill(std::nullopt);
			m_FixedDescriptorSet = VK_NULL_HANDLE;
			m_FixedDescriptorsDirty = true;
		}
		m_CurrentPipeline = pipelineState;
		m_CurrentBindingLayout = bindingLayout;
		vkCmdBindPipeline(m_GraphicsContext->m_CommandBuffer,
			VK_PIPELINE_BIND_POINT_COMPUTE, pipelineState->Get());
	}

	void VulkanComputeCommandContext::SetDescriptorTable(
		const RHIDescriptorTableBinding& binding) noexcept
	{
		if (binding.m_HeapType != RHIDescriptorHeapType::CbvSrvUav &&
			binding.m_HeapType != RHIDescriptorHeapType::Sampler)
		{
			Reject("SetDescriptorTable", "the descriptor heap type is not shader visible");
		}
	}

	void VulkanComputeCommandContext::SetConstantBuffer(
		uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset) noexcept
	{
		(void)SetBufferDescriptor(parameterIndex, buffer, offset,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, RHIBufferUsage::Constant, "SetConstantBuffer");
	}

	void VulkanComputeCommandContext::SetReadOnlyBuffer(
		uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset) noexcept
	{
		(void)SetBufferDescriptor(parameterIndex, buffer, offset,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RHIBufferUsage::Structured, "SetReadOnlyBuffer");
	}

	void VulkanComputeCommandContext::SetReadWriteBuffer(
		uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset) noexcept
	{
		(void)SetBufferDescriptor(parameterIndex, buffer, offset,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RHIBufferUsage::UnorderedAccess,
			"SetReadWriteBuffer");
	}

	void VulkanComputeCommandContext::SetPushConstants(uint32_t parameterIndex,
		std::span<const uint32_t> values, uint32_t destOffset) noexcept
	{
		if (m_CurrentBindingLayout == nullptr)
		{
			Reject("SetPushConstants", "no pipeline binding layout is active");
			return;
		}
		const VulkanDynamicUniformUpdate update = m_DynamicUniformState.SetPushConstants(
			parameterIndex, values, destOffset, *m_GraphicsContext->m_UniformBuffer,
			m_GraphicsContext->m_FrameSlotIndex);
		if (!update.IsValid() || update.m_DynamicOffsetSlot >= m_DynamicOffsets.size())
		{
			Reject("SetPushConstants", "the immutable dynamic-uniform allocation failed");
			return;
		}
		m_DynamicOffsets[update.m_DynamicOffsetSlot] = update.m_Allocation.m_DynamicOffset;
		TrackBufferUse(update.m_Allocation.m_Buffer);
	}

	void VulkanComputeCommandContext::Dispatch(
		uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) noexcept
	{
		if (m_GraphicsContext->m_CommandBuffer == VK_NULL_HANDLE ||
			m_GraphicsContext->m_IsRendering || m_CurrentPipeline == nullptr ||
			groupCountX == 0 || groupCountY == 0 || groupCountZ == 0)
		{
			Reject("Dispatch", m_GraphicsContext->m_IsRendering
				? "dispatch is invalid inside rendering"
				: "the command buffer, pipeline or group counts are invalid");
			return;
		}
		if (!BindDescriptorSets())
		{
			return;
		}
		vkCmdDispatch(m_GraphicsContext->m_CommandBuffer, groupCountX, groupCountY, groupCountZ);
	}

	void VulkanComputeCommandContext::Reject(
		std::string_view operation, std::string_view reason) noexcept
	{
		m_GraphicsContext->Reject(operation, reason);
	}

	bool VulkanComputeCommandContext::SetBufferDescriptor(uint32_t parameterIndex,
		RHIBufferHandle buffer, uint64_t offset, VkDescriptorType descriptorType,
		RHIBufferUsage requiredUsage, std::string_view operation) noexcept
	{
		VulkanSet0BufferBinding next{};
		if (m_CurrentBindingLayout == nullptr || parameterIndex >= m_FixedBufferBindings.size() ||
			!BuildSet0BufferBinding(*m_GraphicsContext->m_Device, *m_CurrentBindingLayout,
				parameterIndex, buffer, offset, descriptorType, requiredUsage, next))
		{
			if (parameterIndex < m_FixedBufferBindings.size())
			{
				m_FixedBufferBindings[parameterIndex].reset();
				m_FixedDescriptorsDirty = true;
			}
			Reject(operation, "the fixed-buffer binding is invalid for the active layout");
			return false;
		}
		if (m_FixedBufferBindings[parameterIndex] != next)
		{
			m_FixedBufferBindings[parameterIndex] = next;
			m_FixedDescriptorsDirty = true;
		}
		TrackBufferUse(buffer);
		return true;
	}

	bool VulkanComputeCommandContext::BindDescriptorSets() noexcept
	{
		if (m_CurrentBindingLayout == nullptr)
		{
			Reject("Dispatch", "no pipeline binding layout is active");
			return false;
		}
		const std::vector fixedBindings = CollectBufferBindings(m_FixedBufferBindings);
		if (m_FixedDescriptorsDirty)
		{
			m_FixedDescriptorSet = m_GraphicsContext->m_Set0Frames->AllocateDescriptorSet(
				m_GraphicsContext->m_FrameSlotIndex, *m_CurrentBindingLayout, fixedBindings);
			m_FixedDescriptorsDirty = false;
		}
		const std::array sets{
			m_FixedDescriptorSet,
			m_GraphicsContext->m_Device->GetDescriptorManager().GetGlobalSet(),
		};
		if (sets[0] == VK_NULL_HANDLE || sets[1] == VK_NULL_HANDLE)
		{
			Reject("Dispatch", "the descriptor-set snapshot is unavailable");
			return false;
		}
		vkCmdBindDescriptorSets(m_GraphicsContext->m_CommandBuffer,
			VK_PIPELINE_BIND_POINT_COMPUTE, m_CurrentBindingLayout->GetPipelineLayout(), 0,
			static_cast<uint32_t>(sets.size()), sets.data(),
			static_cast<uint32_t>(m_DynamicOffsets.size()), m_DynamicOffsets.data());
		return true;
	}

	bool VulkanGraphicsCommandContext::ValidateDraw(
		std::string_view operation, bool indexed) noexcept
	{
		if (!m_IsRendering || m_CurrentPipeline == nullptr || !m_CurrentPipelineDesc ||
			!m_ActiveRenderingSignature || !m_ViewportSet || !m_ScissorSet ||
			m_PrimitiveTopology != m_CurrentPipelineDesc->m_PrimitiveTopology ||
			(indexed && !m_IndexBufferBinding))
		{
			Reject(operation, "required rendering, pipeline, dynamic state or index state is missing");
			return false;
		}
		if (!IsGraphicsPipelineCompatible(*m_CurrentPipelineDesc, *m_ActiveRenderingSignature))
		{
			Reject(operation, "the pipeline is incompatible with the active attachments");
			return false;
		}
		for (uint32_t index = 0;
			index < m_CurrentPipelineDesc->m_VertexInput.m_VertexBufferCount; ++index)
		{
			const RHIVertexBufferLayoutDesc& layout =
				m_CurrentPipelineDesc->m_VertexInput.m_VertexBuffers[index];
			if (layout.m_InputSlot >= m_VertexBindings.size() ||
				!m_VertexBindings[layout.m_InputSlot] ||
				m_VertexBindings[layout.m_InputSlot]->m_Stride != layout.m_StrideInBytes)
			{
				Reject(operation, "a required vertex binding is missing or has a mismatched stride");
				return false;
			}
		}
		return true;
	}
}
