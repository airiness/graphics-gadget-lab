#include "Core/Precompiled.h"
#include "Graphics/TransferBatch.h"

namespace gglab
{
	TransferBatch::TransferBatch(RHITransferContext& transferContext) noexcept :
		m_TransferContext(transferContext)
	{
	}

	bool TransferBatch::UploadBuffer(
		RHIBufferHandle dstBuffer,
		uint64_t dstOffset,
		const void* src,
		uint64_t numBytes) noexcept
	{
		if (!dstBuffer.IsValid() || !src || numBytes == 0)
		{
			GGLAB_LOG_GRAPHICS_WARN("TransferBatch rejected an invalid RHI buffer write.");
			return false;
		}

		return m_TransferContext.UploadBuffer(src, numBytes, dstBuffer, dstOffset);
	}

	bool TransferBatch::UploadTexture(
		RHITextureHandle dstTexture,
		const RHITextureUploadData& uploadData) noexcept
	{
		return m_TransferContext.UploadTexture(uploadData, dstTexture);
	}

	void TransferBatch::CopyBuffer(
		RHIBufferHandle dst,
		uint64_t dstOffset,
		RHIBufferHandle src,
		uint64_t srcOffset,
		uint64_t numBytes) noexcept
	{
		m_TransferContext.CopyBuffer(dst, dstOffset, src, srcOffset, numBytes);
	}

	void TransferBatch::TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept
	{
		m_TransferContext.TextureBarrier(barriers);
	}

	void TransferBatch::BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept
	{
		m_TransferContext.BufferBarrier(barriers);
	}

	RHIFencePoint TransferBatch::Submit(bool wait) noexcept
	{
		return m_TransferContext.Submit(wait);
	}
}
