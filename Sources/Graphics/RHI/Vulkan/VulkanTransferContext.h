#pragma once
#include "Graphics/RHI/RHITransferContext.h"

#include <vulkan/vulkan.h>

#include <memory>
#include <string_view>
#include <vector>

namespace gglab
{
	class VulkanDevice;

	// Vulkan transfer encoder. It aliases the graphics native queue and
	// timeline, so every queue operation is serialized by the device owner
	// thread. One transient command pool is retained per in-flight batch;
	// staging buffers and pools retire together after the submitted timeline
	// point completes.
	class VulkanTransferContext final : public RHITransferContext
	{
	public:
		explicit VulkanTransferContext(VulkanDevice* device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanTransferContext);
		~VulkanTransferContext() override;

		RHICommandContextHandle GetHandle() const noexcept override { return m_Handle; }
		RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Transfer; }
		void TrackTextureUse(RHITextureHandle texture) noexcept override;
		void TrackBufferUse(RHIBufferHandle buffer) noexcept override;
		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override;
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override;
		void FlushBarriers() noexcept override {}

		void Begin() noexcept override;
		[[nodiscard]] RHIFencePoint Submit(bool wait = false) noexcept override;
		void Abort() noexcept override;
		void ReclaimCompleted() noexcept override;
		void CopyBuffer(RHIBufferHandle dst, uint64_t dstOffset, RHIBufferHandle src,
			uint64_t srcOffset, uint64_t numBytes) noexcept override;
		[[nodiscard]] bool UploadBuffer(const void* data, uint64_t sizeInBytes,
			RHIBufferHandle dst, uint64_t dstOffset = 0) noexcept override;
		[[nodiscard]] bool UploadTexture(
			const RHITextureUploadData& uploadData, RHITextureHandle dst) noexcept override;
		[[nodiscard]] RHITextureReadbackRequest ReadbackTexture(
			RHITextureHandle src, const RHITextureDesc& desc) noexcept override;

	private:
		struct InFlightInfo
		{
			RHIFencePoint m_FencePoint{};
			VkCommandPool m_CommandPool = VK_NULL_HANDLE;
			VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
			std::vector<RHIBufferOwner> m_IntermediateBuffers;
			std::vector<RHIBufferHandle> m_UsedBuffers;
			std::vector<RHITextureHandle> m_UsedTextures;
		};

		[[nodiscard]] bool CheckRecording(std::string_view operation) noexcept;
		[[nodiscard]] RHIBufferOwner CreateStagingBuffer(uint64_t sizeInBytes,
			RHIBufferUsage usage, RHIMemoryUsage memoryUsage, std::string_view owner) noexcept;
		void RecordBufferUse(RHIBufferHandle buffer) noexcept;
		void RecordTextureUse(RHITextureHandle texture) noexcept;
		void DestroyInfo(InFlightInfo& info) noexcept;

		RHICommandContextHandle m_Handle{};
		VulkanDevice* m_Device = nullptr;
		std::unique_ptr<InFlightInfo> m_ExecutingInfo;
		std::vector<std::unique_ptr<InFlightInfo>> m_InFlightInfos;
		uint64_t m_NextDebugOperationSerial = 1;
		bool m_RecordingFailed = false;
	};
}
