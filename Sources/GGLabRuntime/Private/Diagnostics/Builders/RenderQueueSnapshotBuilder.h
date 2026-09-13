#pragma once

#include "GGLabRuntime/Diagnostics/Snapshots/RenderQueueSnapshot.h"

#include <span>

namespace gglab
{
	struct RenderQueue;
	[[nodiscard]] RenderQueueSnapshot BuildRenderQueueSnapshot(std::span<const RenderQueue> queues) noexcept;
}
