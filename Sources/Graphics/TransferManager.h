#pragma once
#include "Graphics/RHI/DX12/DX12FencePoint.h"
#include "Graphics/TransferBatch.h"

namespace gglab
{
	class DX12Device;
	class RHITransferContext;

	class TransferManager
	{
	public:
		explicit TransferManager(DX12Device* dx12Device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TransferManager);
		~TransferManager() = default;

		DX12FencePoint Reclaim() noexcept;

		TransferBatch BeginBatch() noexcept;

		RHITransferContext* GetTransferContext() noexcept { return m_TransferContext.get(); }
		const RHITransferContext* GetTransferContext() const noexcept { return m_TransferContext.get(); }

	private:
		std::unique_ptr<RHITransferContext> m_TransferContext;
	};
}
