#pragma once
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHITexture.h"

#include <span>

namespace gglab
{
	struct RHITextureBarrier
	{
		RHITextureHandle m_Texture{};
		RHIResourceState m_Before{};
		RHIResourceState m_After{};
	};

	struct RHIBufferBarrier
	{
		RHIBufferHandle m_Buffer{};
		RHIResourceState m_Before{};
		RHIResourceState m_After{};
	};

	class RHICommandList
	{
	public:
		virtual ~RHICommandList() = default;

		virtual RHIQueueType GetQueueType() const noexcept = 0;
		virtual void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept = 0;
		virtual void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept = 0;
	};
}
