#pragma once
#include "Graphics/RHI/DX12/DX12FencePoint.h"
#include "Graphics/TransferBatch.h"

namespace gglab
{
	class DX12Device;
	class DX12Buffer;
	class DX12RingBuffer;

	class TransferManager
	{
	public:
		explicit TransferManager(DX12Device* dx12Device, uint32_t uploadBufferCapacity) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TransferManager);
		~TransferManager() = default;

		DX12FencePoint Reclaim() noexcept;

		TransferBatch BeginBatch() noexcept;

		DX12RingBuffer* GetUploadRingBuffer() noexcept { return &m_UploadRingBuffer; }
		const DX12RingBuffer* GetUploadRingBuffer() const noexcept { return &m_UploadRingBuffer; }
		CopyContext* GetCopyContext() noexcept { return m_CopyContext.get(); }

	private:
		DX12RingBuffer m_UploadRingBuffer;

		std::unique_ptr<CopyContext> m_CopyContext;
	};
}