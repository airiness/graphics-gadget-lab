#include "Core/Precompiled.h"
#include "Graphics/TransferBatch.h"

namespace gglab
{
	TransferBatch::TransferBatch(RHITransferContext& transferContext) noexcept :
		m_TransferContext(&transferContext)
	{
	}

	TransferBatch::TransferBatch(TransferBatch&& other) noexcept :
		m_TransferContext(std::exchange(other.m_TransferContext, nullptr))
	{
	}

	TransferBatch& TransferBatch::operator=(TransferBatch&& other) noexcept
	{
		if (this != &other)
		{
			AbortIfActive();
			m_TransferContext = std::exchange(other.m_TransferContext, nullptr);
		}
		return *this;
	}

	TransferBatch::~TransferBatch()
	{
		AbortIfActive();
	}

	bool TransferBatch::UploadBuffer(
		RHIBufferHandle dstBuffer, uint64_t dstOffset, const void* src, uint64_t numBytes) noexcept
	{
		if (!dstBuffer.IsValid() || !src || numBytes == 0)
		{
			GGLAB_LOG_GRAPHICS_WARN("TransferBatch rejected an invalid RHI buffer write.");
			return false;
		}

		return m_TransferContext->UploadBuffer(src, numBytes, dstBuffer, dstOffset);
	}

	bool TransferBatch::UploadTexture(
		RHITextureHandle dstTexture, const RHITextureUploadData& uploadData) noexcept
	{
		return m_TransferContext->UploadTexture(uploadData, dstTexture);
	}

	RHITextureReadbackRequest TransferBatch::ReadbackTexture(
		RHITextureHandle srcTexture, const RHITextureDesc& desc) noexcept
	{
		return m_TransferContext->ReadbackTexture(srcTexture, desc);
	}

	void TransferBatch::CopyBuffer(RHIBufferHandle dst, uint64_t dstOffset, RHIBufferHandle src,
		uint64_t srcOffset, uint64_t numBytes) noexcept
	{
		m_TransferContext->CopyBuffer(dst, dstOffset, src, srcOffset, numBytes);
	}

	void TransferBatch::TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept
	{
		m_TransferContext->TextureBarrier(barriers);
	}

	void TransferBatch::BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept
	{
		m_TransferContext->BufferBarrier(barriers);
	}

	RHIFencePoint TransferBatch::Submit(bool wait) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_TransferContext);
		RHITransferContext* transferContext = std::exchange(m_TransferContext, nullptr);
		return transferContext->Submit(wait);
	}

	void TransferBatch::AbortIfActive() noexcept
	{
		if (m_TransferContext)
		{
			RHITransferContext* transferContext = std::exchange(m_TransferContext, nullptr);
			transferContext->Abort();
		}
	}
}
