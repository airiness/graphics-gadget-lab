#pragma once

#include <cstdint>

namespace gglab
{
	enum class BufferAllocationType : uint8_t
	{
		Persistent,
		Dynamic,
	};
}
