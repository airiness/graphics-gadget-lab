#include "Core/Precompiled.h"
#include "Graphics/TransferBatch.h"
#include "Graphics/TransferManager.h"

namespace gglab
{
	TransferBatch::TransferBatch(TransferManager& transferManager,
		CopyContext& copyContext,
		DX12RingBuffer& uploadRing) noexcept :
		m_TransferManager(transferManager),
		m_CopyContext(copyContext),
		m_UploadRing(uploadRing)
	{
	}

	TransferBatch::~TransferBatch()
	{
		GGLAB_ASSERT_MSG(m_UploadSpans.empty() && m_PostSubmitCallbacks.empty(),
			"TransferBatch destroyed without submitting. Call Submit() before destroying the TransferBatch.");
	}

	DX12FencePoint TransferBatch::Submit(bool wait) noexcept
	{
		DX12FencePoint fencePoint = m_TransferManager.GetCopyContext()->End(wait);

		for (auto& span : m_UploadSpans)
		{
			m_UploadRing.Retire(span, fencePoint);
		}

		m_UploadSpans.clear();

		for (auto& callback : m_PostSubmitCallbacks)
		{
			callback(fencePoint);
		}

		m_PostSubmitCallbacks.clear();

		return fencePoint;
	}
}