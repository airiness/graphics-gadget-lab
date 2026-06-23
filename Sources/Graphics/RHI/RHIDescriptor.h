#pragma once
#include "Graphics/RHI/RHIHandles.h"

#include <cstdint>

namespace gglab
{
	enum class RHIDescriptorHeapType : uint8_t
	{
		CbvSrvUav,
		Sampler,
		RenderTarget,
		DepthStencil,
	};

	struct RHIDescriptorHandle
	{
		RHIDescriptorHeapType m_HeapType = RHIDescriptorHeapType::CbvSrvUav;
		uint32_t m_Index = RHITextureViewHandle::InvalidIndex;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Index != RHITextureViewHandle::InvalidIndex;
		}
	};
}
