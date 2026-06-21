#include "Core/Precompiled.h"
#include "Graphics/TransferBatch.h"
#include "Graphics/TransferManager.h"

namespace gglab
{
	TransferBatch::TransferBatch(TransferManager& transferManager,
		CopyContext& copyContext,
		DX12RingBuffer& uploadRing) noexcept :
		m_TransferManager(transferManager),
		m_CopyContext(copyContext),
		m_UploadRing(uploadRing)
	{
	}

	TransferBatch::~TransferBatch()
	{
		GGLAB_ASSERT_MSG(m_UploadSpans.empty(),
			"TransferBatch destroyed without submitting. Call Submit() before destroying the TransferBatch.");
	}

	bool TransferBatch::StageBufferWrite(
		DX12Buffer* dstBuffer,
		uint64_t dstOffset,
		const void* src,
		uint32_t numBytes,
		uint32_t alignment) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(dstBuffer);
		GGLAB_ASSERT_NOT_NULL(src);
		GGLAB_ASSERT_MSG(numBytes > 0,
			"TransferBatch::StageBufferWrite requires at least one byte.");
		GGLAB_ASSERT_MSG(dstBuffer->IsValid(),
			"TransferBatch::StageBufferWrite destination buffer is invalid.");
		GGLAB_ASSERT_MSG(dstOffset <= dstBuffer->SizeInBytes() &&
			numBytes <= dstBuffer->SizeInBytes() - dstOffset,
			"TransferBatch::StageBufferWrite exceeds the destination buffer range.");

		if (!dstBuffer ||
			!src ||
			numBytes == 0 ||
			!dstBuffer->IsValid() ||
			dstOffset > dstBuffer->SizeInBytes() ||
			numBytes > dstBuffer->SizeInBytes() - dstOffset)
		{
			return false;
		}

		auto uploadSpan = ReserveUpload(numBytes, alignment);
		if (!uploadSpan.IsValid())
		{
			GGLAB_ASSERT_MSG(false,
				"TransferBatch::StageBufferWrite failed to allocate upload ring buffer space.");
			return false;
		}

		CommitBufferWrite(
			dstBuffer,
			dstOffset,
			src,
			numBytes,
			uploadSpan);
		return true;
	}

	DX12RingBuffer::Span TransferBatch::ReserveUpload(
		uint32_t numBytes,
		uint32_t alignment) noexcept
	{
		auto uploadSpan = m_UploadRing.Allocate(numBytes, alignment);
		if (!uploadSpan.IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"TransferBatch failed to reserve {} bytes in the upload ring.",
				numBytes);
			return {};
		}

		// Record the reservation immediately. If a later target allocation fails,
		// Submit() still retires this unused upload span safely.
		m_UploadSpans.push_back(uploadSpan);
		return uploadSpan;
	}

	void TransferBatch::CommitBufferWrite(
		DX12Buffer* dstBuffer,
		uint64_t dstOffset,
		const void* src,
		uint32_t numBytes,
		const DX12RingBuffer::Span& uploadSpan) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(dstBuffer);
		GGLAB_ASSERT_NOT_NULL(src);
		GGLAB_ASSERT_MSG(uploadSpan.IsValid(),
			"TransferBatch::CommitBufferWrite requires a valid upload span.");

		std::memcpy(
			m_UploadRing.GetMappedDataAtOffset(uploadSpan.m_Offset),
			src,
			numBytes);

		m_CopyContext.CopyBuffer(
			dstBuffer,
			dstOffset,
			m_UploadRing.GetBuffer(),
			uploadSpan.m_Offset,
			numBytes);
	}

	DX12FencePoint TransferBatch::Submit(bool wait) noexcept
	{
		DX12FencePoint fencePoint = m_TransferManager.GetCopyContext()->End(wait);

		for (auto& span : m_UploadSpans)
		{
			m_UploadRing.Retire(span, fencePoint);
		}

		m_UploadSpans.clear();

		return fencePoint;
	}
}
