#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"

#include <Windows.h>

namespace gglab
{
	// The host owns the window, which must outlive the returned context.
	[[nodiscard]] std::unique_ptr<RHIContext> CreateDX12Context(
		const RHIContextDesc& desc, HWND window) noexcept;
}
