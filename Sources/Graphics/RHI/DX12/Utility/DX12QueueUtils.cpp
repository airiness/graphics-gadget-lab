#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Utility/DX12QueueUtils.h"

namespace gglab
{
	D3D12_COMMAND_LIST_TYPE ToD3D12CommandListType(RHIQueueType queueType) noexcept
	{
		switch (queueType)
		{
		case RHIQueueType::Graphics:
			return D3D12_COMMAND_LIST_TYPE_DIRECT;
		case RHIQueueType::Compute:
			return D3D12_COMMAND_LIST_TYPE_COMPUTE;
		case RHIQueueType::Copy:
			return D3D12_COMMAND_LIST_TYPE_COPY;
		}

		GGLAB_UNREACHABLE("Unhandled RHIQueueType.");
	}
}
