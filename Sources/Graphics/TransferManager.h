#pragma once
#include "Graphics/TransferBatch.h"

#include <memory>

namespace gglab
{
	class RHIDevice;
	class RHITransferContext;

	class TransferManager
	{
	public:
		explicit TransferManager(RHIDevice* device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TransferManager);
		~TransferManager() = default;

		void Reclaim() noexcept;

		TransferBatch BeginBatch() noexcept;

	private:
		std::unique_ptr<RHITransferContext> m_TransferContext;
	};
}
