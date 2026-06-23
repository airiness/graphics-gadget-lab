#pragma once
#include "Graphics/RHI/RHIHandles.h"
#include "Graphics/RHI/RHITypes.h"

#include <cstdint>

namespace gglab
{
	struct RHIFencePoint
	{
		RHIFenceHandle m_Fence{};
		uint64_t m_Value = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Fence.IsValid() && m_Value != 0;
		}
	};
}
