#pragma once
#include "DX12RingStructuredBuffer.h"
#include "TransferBatch.h"

#include <memory>

namespace gglab
{
	template<typename T>
	struct StructuredBuffer
	{
		std::unique_ptr<DX12RingStructuredBuffer<T>> m_StructuredBuffer;
		TransferBatch::StageBufferWriteResult<T> m_BufferRange{};
	};
}