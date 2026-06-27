#include "Core/Precompiled.h"
#include "Graphics/TransferManager.h"
#include "Graphics/RHI/RHITransferContext.h"
#include "Graphics/RHI/DX12/DX12Device.h"

namespace gglab
{
	TransferManager::TransferManager(DX12Device* dx12Device) noexcept :
		m_TransferContext(std::make_unique<RHITransferContext>(dx12Device))
	{
	}

	DX12FencePoint TransferManager::Reclaim() noexcept
	{
		return m_TransferContext->ReclaimCompleted();
	}

	TransferBatch TransferManager::BeginBatch() noexcept
	{
		// Reclaim completed uploads before starting a new batch
		Reclaim();

		m_TransferContext->Begin();
		return TransferBatch(*m_TransferContext);
	}
}
