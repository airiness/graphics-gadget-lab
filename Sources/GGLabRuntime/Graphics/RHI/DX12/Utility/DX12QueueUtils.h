#pragma once
#include "Graphics/RHI/RHIResource.h"

#include <d3d12.h>

namespace gglab
{
	[[nodiscard]] D3D12_COMMAND_LIST_TYPE ToD3D12CommandListType(RHIQueueType queueType) noexcept;
}
