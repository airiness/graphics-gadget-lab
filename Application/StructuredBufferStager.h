#pragma once
#include "DX12ResourceUploader.h"
#include "DX12RingBuffer.h"
#include "GPUStructures.h"

namespace gglab
{
	class StrcturedBufferStager
	{
	public:
		StrcturedBufferStager(DX12ResourceUploader& uploader, DX12RingBuffer& uploadBuffer) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(StrcturedBufferStager);
		~StrcturedBufferStager() = default;

		void BeginBatch() noexcept;
		void StageWrite(DX12Resource* dst, uint32_t dstOffset,
			const void* src, uint32_t sizeInBytes,
			uint32_t alignment = GGLAB_GPU_STRUCTURE_ALIGNAS_VALUE) noexcept;
		DX12FencePoint Flush(bool wait = false) noexcept;

	private:
		DX12ResourceUploader& m_Uploader;
		DX12RingBuffer& m_UploadBuffer;
		std::vector<DX12RingBuffer::Span> m_Pendings;
	};
}