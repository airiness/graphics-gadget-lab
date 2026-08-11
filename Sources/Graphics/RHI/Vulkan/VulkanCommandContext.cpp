#include "Core/Precompiled.h"
#include "Graphics/RHI/Vulkan/VulkanCommandContext.h"
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
	}

	bool IsVulkanIndexBufferOffsetAligned(RHIFormat format, uint64_t offset) noexcept
	{
		return format == RHIFormat::R32Uint && offset % sizeof(uint32_t) == 0;
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
			m_Set0Frames->GetDescriptorSet(frameSlotIndex) == VK_NULL_HANDLE)
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
		m_VertexBindings.fill(std::nullopt);
		m_PrimitiveTopology = RHIPrimitiveTopology::Unknown;
		m_RenderExtent = {};
		m_IsRendering = false;
		m_ViewportSet = false;
		m_ScissorSet = false;
		m_IndexBufferSet = false;
		m_HasEncodingError = false;
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
		if (!barriers.empty())
		{
			Reject("TextureBarrier", "graphics barrier recording is not available");
		}
	}

	void VulkanGraphicsCommandContext::BufferBarrier(
		std::span<const RHIBufferBarrier> barriers) noexcept
	{
		if (!barriers.empty())
		{
			Reject("BufferBarrier", "graphics barrier recording is not available");
		}
	}

	void VulkanGraphicsCommandContext::FlushBarriers() noexcept
	{
	}

	void VulkanGraphicsCommandContext::CopyBuffer(RHIBufferHandle destination,
		uint64_t destinationOffset, RHIBufferHandle source, uint64_t sourceOffset,
		uint64_t sizeInBytes) noexcept
	{
		GGLAB_UNUSED(destination);
		GGLAB_UNUSED(destinationOffset);
		GGLAB_UNUSED(source);
		GGLAB_UNUSED(sourceOffset);
		GGLAB_UNUSED(sizeInBytes);
		Reject("CopyBuffer", "copy recording is not available on the graphics encoder");
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
		GGLAB_UNUSED(parameterIndex);
		GGLAB_UNUSED(buffer);
		GGLAB_UNUSED(offset);
		Reject("SetConstantBuffer", "fixed buffer descriptor recording is not available");
	}

	void VulkanGraphicsCommandContext::SetReadOnlyBuffer(
		uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset) noexcept
	{
		GGLAB_UNUSED(parameterIndex);
		GGLAB_UNUSED(buffer);
		GGLAB_UNUSED(offset);
		Reject("SetReadOnlyBuffer", "fixed buffer descriptor recording is not available");
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
		VulkanBuffer* buffer = m_Device->GetResourceManager().ResolveBuffer(binding.m_Buffer);
		const RHIBufferDesc* desc = m_Device->GetResourceManager().ResolveBufferDesc(binding.m_Buffer);
		if (m_CommandBuffer == VK_NULL_HANDLE || buffer == nullptr || desc == nullptr ||
			!Test(desc->m_Usage, RHIBufferUsage::Index) || binding.m_Format != RHIFormat::R32Uint ||
			!IsVulkanIndexBufferOffsetAligned(binding.m_Format, binding.m_Offset) ||
			binding.m_Offset > desc->m_SizeInBytes || binding.m_SizeInBytes == 0 ||
			binding.m_SizeInBytes > desc->m_SizeInBytes - binding.m_Offset)
		{
			Reject("SetIndexBuffer", "the index buffer binding is invalid");
			return;
		}
		vkCmdBindIndexBuffer(m_CommandBuffer, buffer->Get(), binding.m_Offset, VK_INDEX_TYPE_UINT32);
		m_IndexBufferSet = true;
		TrackBufferUse(binding.m_Buffer);
	}

	void VulkanGraphicsCommandContext::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
		uint32_t startIndexLocation, int32_t baseVertexLocation,
		uint32_t startInstanceLocation) noexcept
	{
		if (indexCount == 0 || instanceCount == 0 || !ValidateDraw("DrawIndexed", true) ||
			!BindDescriptorSets())
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

	bool VulkanGraphicsCommandContext::BindDescriptorSets() noexcept
	{
		if (m_CurrentBindingLayout == nullptr)
		{
			Reject("Draw", "no pipeline binding layout is active");
			return false;
		}
		const std::array sets{
			m_Set0Frames->GetDescriptorSet(m_FrameSlotIndex),
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

	bool VulkanGraphicsCommandContext::ValidateDraw(
		std::string_view operation, bool indexed) noexcept
	{
		if (!m_IsRendering || m_CurrentPipeline == nullptr || !m_CurrentPipelineDesc ||
			!m_ActiveRenderingSignature || !m_ViewportSet || !m_ScissorSet ||
			m_PrimitiveTopology != m_CurrentPipelineDesc->m_PrimitiveTopology ||
			(indexed && !m_IndexBufferSet))
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
