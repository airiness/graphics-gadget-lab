#pragma once
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHITransferContext.h"

namespace gglab
{
	class TransferBatch
	{
	public:
		explicit TransferBatch(RHITransferContext& transferContext) noexcept;
		GGLAB_DELETE_COPYABLE(TransferBatch);
		TransferBatch(TransferBatch&& other) noexcept;
		TransferBatch& operator=(TransferBatch&& other) noexcept;
		~TransferBatch();

		bool UploadBuffer(RHIBufferHandle dstBuffer, uint64_t dstOffset, const void* src,
			uint64_t numBytes) noexcept;
		bool UploadTexture(RHITextureHandle dstTexture, const RHITextureUploadData& uploadData,
			RHIResourceState initialState, RHIResourceState publishedState,
			std::optional<RHISubresourceRange> subresources = std::nullopt) noexcept;
		[[nodiscard]] RHITextureReadbackRequest ReadbackTexture(
			RHITextureHandle srcTexture, const RHITextureDesc& desc) noexcept;
		void CopyBuffer(RHIBufferHandle dst, uint64_t dstOffset, RHIBufferHandle src,
			uint64_t srcOffset, uint64_t numBytes) noexcept;
		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept;
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept;
		bool PublishTexture(RHITextureHandle texture, RHIResourceState publishedState,
			std::optional<RHISubresourceRange> subresources = std::nullopt) noexcept;
		bool PublishBuffer(RHIBufferHandle buffer, RHIResourceState publishedState) noexcept;

		[[nodiscard]] RHITransferSubmission Submit(bool wait = false) noexcept;

	private:
		void AbortIfActive() noexcept;

		RHITransferContext* m_TransferContext = nullptr;
		std::vector<RHITransferResourcePublication> m_Publications;
	};
}
