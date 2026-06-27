#pragma once
#include "Graphics/RHI/DX12/DX12FencePoint.h"
#include "Graphics/RHI/DX12/DX12RingBuffer.h"
#include "Graphics/RHI/RHITransferContext.h"
#include "Graphics/GPUStructures.h"

#include <limits>

namespace gglab
{
	class DX12Device;
	class DX12Buffer;
	class DX12RingBuffer;
	class TransferManager;
	class TransferBatch
	{
	public:
		TransferBatch(TransferManager& transferManager,
			RHITransferContext& transferContext,
			DX12RingBuffer& uploadRing) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(TransferBatch);
		~TransferBatch();

		bool StageBufferWrite(DX12Buffer* dstBuffer, uint64_t dstOffset,
			const void* src, uint32_t numBytes,
			uint32_t alignment = GGLAB_GPU_STRUCTURE_ALIGNAS_VALUE) noexcept;
		bool StageBufferWrite(RHIBufferHandle dstBuffer, uint64_t dstOffset,
			const void* src, uint64_t numBytes) noexcept;

		DX12FencePoint Submit(bool wait = false) noexcept;

	private:
		DX12RingBuffer::Span ReserveUpload(
			uint32_t numBytes,
			uint32_t alignment) noexcept;
		void CommitBufferWrite(
			DX12Buffer* dstBuffer,
			uint64_t dstOffset,
			const void* src,
			uint32_t numBytes,
			const DX12RingBuffer::Span& uploadSpan) noexcept;

	private:
		TransferManager& m_TransferManager;
		RHITransferContext& m_TransferContext;
		DX12RingBuffer& m_UploadRing;

		std::vector<DX12RingBuffer::Span> m_UploadSpans;
	};
}
