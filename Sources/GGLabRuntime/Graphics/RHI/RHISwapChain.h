#pragma once
#include "Graphics/RHI/RHITexture.h"

namespace gglab
{
	class RHISwapChain
	{
	public:
		virtual ~RHISwapChain() = default;

		[[nodiscard]] virtual bool IsValid() const noexcept = 0;

		[[nodiscard]] virtual uint32_t GetBufferCount() const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetBufferWidth() const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetBufferHeight() const noexcept = 0;
		[[nodiscard]] virtual RHIFormat GetFormat() const noexcept = 0;
		[[nodiscard]] virtual RHITextureHandle GetBackBufferHandle(
			uint32_t bufferIndex) const noexcept = 0;
	};
}
