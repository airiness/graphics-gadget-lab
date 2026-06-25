#include "Core/Precompiled.h"
#include "Graphics/TransferManager.h"
#include "Graphics/RHI/RHITransferContext.h"

namespace gglab
{
	TransferManager::TransferManager(DX12Device* dx12Device, uint32_t uploadBufferCapacity) noexcept :
		m_TransferContext(std::make_unique<RHITransferContext>(dx12Device))
	{
		auto createInfo = DX12Buffer::UploadBufferCreateInfo(
			dx12Device->GetMemAllocator(), 
			static_cast<uint64_t>(uploadBufferCapacity));
		m_UploadRingBuffer.Create(createInfo, uploadBufferCapacity);
	}

	DX12FencePoint TransferManager::Reclaim() noexcept
	{
		const auto completedFence = m_TransferContext->ReclaimCompleted();
		if (completedFence.IsValid())
		{
			m_UploadRingBuffer.ReclaimCompleted(completedFence);
		}
		return completedFence;
	}

	TransferBatch TransferManager::BeginBatch() noexcept
	{
		// Reclaim completed uploads before starting a new batch
		Reclaim();

		m_TransferContext->Begin();
		return TransferBatch(*this, *m_TransferContext, m_UploadRingBuffer);
	}
}
