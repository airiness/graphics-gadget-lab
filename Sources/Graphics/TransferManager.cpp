#include "Core/Precompiled.h"
#include "Graphics/TransferManager.h"
#include "Graphics/RHI/RHITransferContext.h"

namespace gglab
{
	TransferManager::TransferManager(std::unique_ptr<RHITransferContext> transferContext) noexcept :
		m_TransferContext(std::move(transferContext))
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
