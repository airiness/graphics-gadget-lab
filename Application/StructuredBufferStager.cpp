#include "Precompiled.h"
#include "StructuredBufferStager.h"

namespace gglab
{
	StrcturedBufferStager::StrcturedBufferStager(DX12ResourceUploader& uploader,
		DX12RingBuffer& uploadBuffer) noexcept :
		m_Uploader(uploader),
		m_UploadBuffer(uploadBuffer)
	{
	}

	void StrcturedBufferStager::BeginBatch() noexcept
	{
		//DX12FencePoint fencePoint = m_Uploader.
	}
}