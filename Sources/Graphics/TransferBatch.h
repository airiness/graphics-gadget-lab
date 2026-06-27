#pragma once
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHITransferContext.h"

namespace gglab
{
	class TransferBatch
	{
	public:
		explicit TransferBatch(RHITransferContext& transferContext) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(TransferBatch);
		~TransferBatch() = default;

		bool UploadBuffer(RHIBufferHandle dstBuffer, uint64_t dstOffset,
			const void* src, uint64_t numBytes) noexcept;
		bool UploadTexture(
			RHITextureHandle dstTexture,
			const RHITextureUploadData& uploadData) noexcept;
		void CopyBuffer(RHIBufferHandle dst, uint64_t dstOffset,
			RHIBufferHandle src, uint64_t srcOffset,
			uint64_t numBytes) noexcept;
		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept;
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept;

		[[nodiscard]] RHIFencePoint Submit(bool wait = false) noexcept;

	private:
		RHITransferContext& m_TransferContext;
	};
}
