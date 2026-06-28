#pragma once
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/RHI/RHISwapChain.h"

#include <memory>

namespace gglab
{
	class TransferManager;

	struct RHIContextDesc
	{
		RHIBackendType m_Backend = RHIBackendType::DX12;
		void* m_WindowHandle = nullptr;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHIFormat m_BackBufferFormat = RHIFormat::R8G8B8A8Unorm;
		uint32_t m_BufferCount = 2;
		bool m_AllowTearing = true;
		bool m_Vsync = true;
	};

	class RHIFrameContext
	{
	public:
		virtual ~RHIFrameContext() = default;

		[[nodiscard]] virtual uint32_t GetBackBufferIndex() const noexcept = 0;
		[[nodiscard]] virtual RHITextureHandle GetBackBuffer() const noexcept = 0;
		[[nodiscard]] virtual RHIGraphicsCommandContext& GetGraphicsContext() noexcept = 0;
		[[nodiscard]] virtual RHIComputeCommandContext* GetComputeContext() noexcept = 0;
		[[nodiscard]] virtual RHIFencePoint GetSubmittedFence() const noexcept = 0;
	};

	class RHIContext
	{
	public:
		virtual ~RHIContext() = default;

		[[nodiscard]] virtual RHIDevice& GetDevice() noexcept = 0;
		[[nodiscard]] virtual const RHIDevice& GetDevice() const noexcept = 0;
		[[nodiscard]] virtual RHISwapChain& GetSwapChain() noexcept = 0;
		[[nodiscard]] virtual const RHISwapChain& GetSwapChain() const noexcept = 0;
		[[nodiscard]] virtual TransferManager& GetTransferManager() noexcept = 0;

		[[nodiscard]] virtual RHIFrameContext& BeginFrame() noexcept = 0;
		[[nodiscard]] virtual RHIFencePoint EndFrame(RHIFrameContext& frame) noexcept = 0;
		virtual void AbortFrame(RHIFrameContext& frame) noexcept = 0;

		virtual void Resize(uint32_t width, uint32_t height) noexcept = 0;
		virtual void WaitIdle() noexcept = 0;
		virtual void RetireCompletedWork() noexcept = 0;

		[[nodiscard]] virtual uint32_t GetCurrentFrameIndex() const noexcept = 0;
	};

	[[nodiscard]] std::unique_ptr<RHIContext> CreateRHIContext(
		const RHIContextDesc& desc) noexcept;
}
