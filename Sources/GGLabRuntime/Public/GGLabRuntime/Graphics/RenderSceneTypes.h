#pragma once

#include <cstdint>

namespace gglab
{
	enum class RenderSceneBuildStatus : uint8_t
	{
		Ready,
		GpuUploadFailed,
	};
}
