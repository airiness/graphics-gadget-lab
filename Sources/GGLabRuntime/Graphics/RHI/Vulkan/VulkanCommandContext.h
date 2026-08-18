#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/Vulkan/VulkanDynamicUniformBuffer.h"
#include "Graphics/RHI/Vulkan/VulkanResourceManager.h"

#include <vulkan/vulkan.h>

#include <array>
#include <optional>
#include <vector>

namespace gglab
{
	class VulkanBindingLayout;
	class VulkanDevice;
	class VulkanPipelineState;
	class VulkanPipelineSystem;
	class VulkanComputeCommandContext;

	class VulkanGraphicsCommandContext final : public RHIGraphicsCommandContext
	{
	public:
		VulkanGraphicsCommandContext(VulkanDevice* device, VulkanPipelineSystem* pipelineSystem,
			VulkanDynamicUniformBuffer* uniformBuffer,
			VulkanSet0DynamicUniformFrames* set0Frames) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanGraphicsCommandContext);
		~VulkanGraphicsCommandContext() override = default;
		using RHIGraphicsCommandContext::SetPushConstants;

		[[nodiscard]] bool BeginEncoding(
			VkCommandBuffer commandBuffer, uint32_t frameSlotIndex) noexcept;
		[[nodiscard]] bool FinishEncoding() noexcept;
		void AbortEncoding() noexcept;
		[[nodiscard]] bool HasEncodingError() const noexcept { return m_HasEncodingError; }
		// Borrowed native command buffer for backend adapters such as the
		// official ImGui Vulkan renderer. Valid only while encoding.
		[[nodiscard]] VkCommandBuffer Get() const noexcept { return m_CommandBuffer; }

		RHICommandContextHandle GetHandle() const noexcept override { return m_Handle; }
		RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Graphics; }
		void TrackTextureUse(RHITextureHandle texture) noexcept override;
		void TrackBufferUse(RHIBufferHandle buffer) noexcept override;
		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override;
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override;
		void FlushBarriers() noexcept override;
		void CopyBuffer(RHIBufferHandle destination, uint64_t destinationOffset,
			RHIBufferHandle source, uint64_t sourceOffset, uint64_t sizeInBytes) noexcept override;
		void SetPipeline(RHIPipelineHandle pipeline) noexcept override;
		void SetDescriptorTable(const RHIDescriptorTableBinding& binding) noexcept override;
		void BeginRendering(const RHIRenderingInfo& info) noexcept override;
		void EndRendering() noexcept override;
		bool IsRendering() const noexcept override { return m_IsRendering; }
		void ClearColorAttachment(
			uint32_t colorAttachmentIndex, const std::array<float, 4>& color) noexcept override;
		void ClearDepthAttachment(float depth,
			std::optional<uint8_t> stencil = std::nullopt) noexcept override;
		void SetViewport(const RHIViewport& viewport) noexcept override;
		void SetScissorRect(const RHIScissorRect& rect) noexcept override;
		void SetPrimitiveTopology(RHIPrimitiveTopology topology) noexcept override;
		void SetConstantBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept override;
		void SetReadOnlyBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept override;
		void SetPushConstants(uint32_t parameterIndex, std::span<const uint32_t> values,
			uint32_t destOffset = 0) noexcept override;
		void SetVertexBuffers(
			uint32_t startSlot, std::span<const RHIVertexBufferBinding> bindings) noexcept override;
		void SetIndexBuffer(const RHIIndexBufferBinding& binding) noexcept override;
		void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
			uint32_t startIndexLocation = 0, int32_t baseVertexLocation = 0,
			uint32_t startInstanceLocation = 0) noexcept override;
		void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
			uint32_t startVertexLocation = 0, uint32_t startInstanceLocation = 0) noexcept override;

		[[nodiscard]] std::span<const RHIBufferHandle> GetUsedBuffers() const noexcept
		{
			return m_UsedBuffers;
		}
		[[nodiscard]] std::span<const RHITextureHandle> GetUsedTextures() const noexcept
		{
			return m_UsedTextures;
		}

	private:
		friend class VulkanComputeCommandContext;
		void Reject(std::string_view operation, std::string_view reason) noexcept;
		[[nodiscard]] bool SetBufferDescriptor(uint32_t parameterIndex, RHIBufferHandle buffer,
			uint64_t offset, VkDescriptorType descriptorType,
			RHIBufferUsage requiredUsage, std::string_view operation) noexcept;
		[[nodiscard]] bool BindDescriptorSets() noexcept;
		[[nodiscard]] bool ValidateDraw(std::string_view operation, bool indexed) noexcept;

		RHICommandContextHandle m_Handle = AllocateRHICommandContextHandle();
		VulkanDevice* m_Device = nullptr;
		VulkanPipelineSystem* m_PipelineSystem = nullptr;
		VulkanDynamicUniformBuffer* m_UniformBuffer = nullptr;
		VulkanSet0DynamicUniformFrames* m_Set0Frames = nullptr;
		VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
		uint32_t m_FrameSlotIndex = 0;
		VulkanPipelineState* m_CurrentPipeline = nullptr;
		VulkanBindingLayout* m_CurrentBindingLayout = nullptr;
		std::optional<RHIGraphicsPipelineDesc> m_CurrentPipelineDesc;
		std::optional<RHIRenderingSignature> m_ActiveRenderingSignature;
		std::vector<VulkanResourceManager::TextureViewBinding> m_ActiveColorAttachments;
		std::optional<VulkanResourceManager::TextureViewBinding> m_ActiveDepthAttachment;
		VulkanDynamicUniformState m_DynamicUniformState;
		std::vector<uint32_t> m_DynamicOffsets;
		std::vector<RHIBufferHandle> m_UsedBuffers;
		std::vector<RHITextureHandle> m_UsedTextures;
		std::vector<VkImageMemoryBarrier2> m_PendingImageBarriers;
		std::vector<VkBufferMemoryBarrier2> m_PendingBufferBarriers;
		std::array<std::optional<VulkanSet0BufferBinding>, RHIBindingLayoutDesc::MaxSlots>
			m_FixedBufferBindings{};
		VkDescriptorSet m_FixedDescriptorSet = VK_NULL_HANDLE;
		bool m_FixedDescriptorsDirty = true;
		std::array<std::optional<RHIVertexBufferBinding>,
			RHIVertexInputLayoutDesc::MaxVertexBuffers> m_VertexBindings{};
		std::optional<RHIIndexBufferBinding> m_IndexBufferBinding;
		RHIPrimitiveTopology m_PrimitiveTopology = RHIPrimitiveTopology::Unknown;
		VkExtent2D m_RenderExtent{};
		bool m_IsRendering = false;
		bool m_ViewportSet = false;
		bool m_ScissorSet = false;
		bool m_HasEncodingError = false;
		VulkanComputeCommandContext* m_DirectComputeContext = nullptr;
	};

	class VulkanComputeCommandContext final : public RHIComputeCommandContext
	{
	public:
		explicit VulkanComputeCommandContext(VulkanGraphicsCommandContext& graphicsContext) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanComputeCommandContext);
		~VulkanComputeCommandContext() override;
		using RHIComputeCommandContext::SetPushConstants;

		RHICommandContextHandle GetHandle() const noexcept override
		{
			return m_GraphicsContext->GetHandle();
		}
		RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Graphics; }
		void TrackTextureUse(RHITextureHandle texture) noexcept override;
		void TrackBufferUse(RHIBufferHandle buffer) noexcept override;
		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override;
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override;
		void FlushBarriers() noexcept override;
		void CopyBuffer(RHIBufferHandle destination, uint64_t destinationOffset,
			RHIBufferHandle source, uint64_t sourceOffset, uint64_t sizeInBytes) noexcept override;
		void SetPipeline(RHIPipelineHandle pipeline) noexcept override;
		void SetDescriptorTable(const RHIDescriptorTableBinding& binding) noexcept override;
		void SetConstantBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept override;
		void SetReadOnlyBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept override;
		void SetReadWriteBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept override;
		void SetPushConstants(uint32_t parameterIndex, std::span<const uint32_t> values,
			uint32_t destOffset = 0) noexcept override;
		void Dispatch(
			uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) noexcept override;

	private:
		friend class VulkanGraphicsCommandContext;
		void ResetEncodingState() noexcept;
		void Reject(std::string_view operation, std::string_view reason) noexcept;
		[[nodiscard]] bool SetBufferDescriptor(uint32_t parameterIndex, RHIBufferHandle buffer,
			uint64_t offset, VkDescriptorType descriptorType,
			RHIBufferUsage requiredUsage, std::string_view operation) noexcept;
		[[nodiscard]] bool BindDescriptorSets() noexcept;

		VulkanGraphicsCommandContext* m_GraphicsContext = nullptr;
		VulkanPipelineState* m_CurrentPipeline = nullptr;
		VulkanBindingLayout* m_CurrentBindingLayout = nullptr;
		VulkanDynamicUniformState m_DynamicUniformState;
		std::vector<uint32_t> m_DynamicOffsets;
		std::array<std::optional<VulkanSet0BufferBinding>, RHIBindingLayoutDesc::MaxSlots>
			m_FixedBufferBindings{};
		VkDescriptorSet m_FixedDescriptorSet = VK_NULL_HANDLE;
		bool m_FixedDescriptorsDirty = true;
	};
}
