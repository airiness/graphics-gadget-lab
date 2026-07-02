#pragma once
#include "Graphics/RHI/RHIResource.h"

#include <dxgi1_6.h>

namespace gglab
{
	[[nodiscard]] DXGI_FORMAT ToDXGIFormat(RHIFormat format) noexcept;
	[[nodiscard]] RHIFormat ToRHIFormat(DXGI_FORMAT format) noexcept;
}
