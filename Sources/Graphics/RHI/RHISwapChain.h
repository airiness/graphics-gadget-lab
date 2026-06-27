#pragma once
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHITexture.h"

namespace gglab
{
	struct RHISwapChainDesc
	{
		void* m_WindowHandle = nullptr;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHIFormat m_Format = RHIFormat::R8G8B8A8Unorm;
		uint32_t m_BufferCount = 2;
		bool m_AllowTearing = true;
		bool m_Vsync = true;
	};

	class RHISwapChain
	{
	public:
		virtual ~RHISwapChain() = default;

		virtual void Finalize() noexcept = 0;
		[[nodiscard]] virtual bool IsValid() const noexcept = 0;
		virtual void Resize(uint32_t width, uint32_t height) noexcept = 0;

		[[nodiscard]] virtual uint32_t GetBufferCount() const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetBufferWidth() const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetBufferHeight() const noexcept = 0;
		[[nodiscard]] virtual RHIFormat GetFormat() const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetCurrentBackBufferIndex() const noexcept = 0;
		[[nodiscard]] virtual RHITextureHandle GetBackBufferHandle(uint32_t bufferIndex) const noexcept = 0;

		virtual void WaitFrameCompletion() noexcept = 0;
		virtual void SetFrameCompletionFence(const RHIFencePoint& fencePoint) noexcept = 0;
		virtual void Present() noexcept = 0;
	};
}
