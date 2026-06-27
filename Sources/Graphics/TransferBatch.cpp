#include "Core/Precompiled.h"
#include "Graphics/TransferBatch.h"

namespace gglab
{
	TransferBatch::TransferBatch(RHITransferContext& transferContext) noexcept :
		m_TransferContext(transferContext)
	{
	}

	bool TransferBatch::StageBufferWrite(
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

	DX12FencePoint TransferBatch::Submit(bool wait) noexcept
	{
		return m_TransferContext.End(wait);
	}
}
