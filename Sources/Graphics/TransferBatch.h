#pragma once
#include "Graphics/RHI/DX12/DX12FencePoint.h"
#include "Graphics/RHI/RHITransferContext.h"

namespace gglab
{
	class DX12Device;
	class TransferBatch
	{
	public:
		explicit TransferBatch(RHITransferContext& transferContext) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(TransferBatch);
		~TransferBatch() = default;

		bool StageBufferWrite(RHIBufferHandle dstBuffer, uint64_t dstOffset,
			const void* src, uint64_t numBytes) noexcept;

		DX12FencePoint Submit(bool wait = false) noexcept;

	private:
		RHITransferContext& m_TransferContext;
	};
}
