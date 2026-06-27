#include "Core/Precompiled.h"
#include "Graphics/TransferManager.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/RHI/RHITransferContext.h"

namespace gglab
{
	TransferManager::TransferManager(RHIDevice* device) noexcept :
		m_TransferContext(device ? device->CreateTransferContext() : nullptr)
	{
		GGLAB_ASSERT_MSG(m_TransferContext != nullptr,
			"TransferManager failed to create an RHI transfer context.");
	}

	void TransferManager::Reclaim() noexcept
	{
		m_TransferContext->ReclaimCompleted();
	}

	TransferBatch TransferManager::BeginBatch() noexcept
	{
		// Reclaim completed uploads before starting a new batch.
		Reclaim();

		m_TransferContext->Begin();
		return TransferBatch(*m_TransferContext);
	}
}
