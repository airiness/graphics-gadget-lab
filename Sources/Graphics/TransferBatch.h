#pragma once
#include "Graphics/RHI/DX12/DX12FencePoint.h"
#include "Graphics/RHI/DX12/DX12RingStructuredBuffer.h"
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
		template<typename T>
		struct StageBufferWriteResult
		{
			uint32_t m_FirstElementIndex = 0;
			uint32_t m_ElementCount = 0;
			typename DX12RingStructuredBuffer<T>::AllocateResult m_TargetAllocation{};
			bool IsValid() const noexcept { return m_ElementCount > 0; }
		};

	public:
		TransferBatch(TransferManager& transferManager,
			RHITransferContext& transferContext,
			DX12RingBuffer& uploadRing) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(TransferBatch);
		~TransferBatch();

		bool StageBufferWrite(DX12Buffer* dstBuffer, uint64_t dstOffset,
			const void* src, uint32_t numBytes,
			uint32_t alignment = GGLAB_GPU_STRUCTURE_ALIGNAS_VALUE) noexcept;

		template<typename T>
		StageBufferWriteResult<T> StageStructuredBufferWrite(
			DX12RingStructuredBuffer<T>& dstStructuredBuffer,
			std::span<const T> srcData) noexcept
		{
			StageBufferWriteResult<T> result{};

			if (srcData.empty())
			{
				return result;
			}

			const auto stride = static_cast<uint32_t>(sizeof(T));
			GGLAB_ASSERT_MSG(srcData.size() <=
				std::numeric_limits<uint32_t>::max() / stride,
				"TransferBatch::StageStructuredBufferWrite byte size overflow.");
			if (srcData.size() > std::numeric_limits<uint32_t>::max() / stride)
			{
				return result;
			}

			GGLAB_ASSERT_MSG(srcData.size() <= std::numeric_limits<uint32_t>::max(),
				"TransferBatch::StageStructuredBufferWrite element count overflow.");
			if (srcData.size() > std::numeric_limits<uint32_t>::max())
			{
				return result;
			}

			const auto bytes = static_cast<uint32_t>(srcData.size() * stride);
			auto uploadSpan = ReserveUpload(bytes, stride);
			GGLAB_ASSERT_MSG(uploadSpan.IsValid(),
				"TransferBatch::StageStructuredBufferWrite: failed to allocate upload buffer space.");
			if (!uploadSpan.IsValid())
			{
				return result;
			}

			auto alloc = dstStructuredBuffer.Allocate(static_cast<uint32_t>(srcData.size()));
			GGLAB_ASSERT_MSG(alloc.IsValid(),
				"TransferBatch::StageStructuredBufferWrite: failed to allocate structured buffer space.");
			if (!alloc.IsValid())
			{
				return result;
			}

			CommitBufferWrite(
				dstStructuredBuffer.GetBuffer(),
				alloc.m_OffsetInBytes,
				srcData.data(),
				bytes,
				uploadSpan);

			result.m_FirstElementIndex = alloc.m_FirstElementIndex;
			result.m_ElementCount = alloc.m_ElementCount;
			result.m_TargetAllocation = alloc;

			return result;
		}

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
