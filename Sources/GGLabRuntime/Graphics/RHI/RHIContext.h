#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/RHI/RHISwapChain.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace gglab
{
	class TransferManager;
	class RHIPipelineSystem;
	class GpuProfiler;

	struct RHIContextDesc
	{
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHIFormat m_BackBufferFormat = RHIFormat::R8G8B8A8Unorm;
		uint32_t m_FrameSlotCount = 2;
		std::optional<std::string> m_AdapterSelector;
		bool m_EnableDebugValidation = false;
		bool m_AllowTearing = true;
		bool m_Vsync = false;
	};

	class RHIFrameContext
	{
	public:
		virtual ~RHIFrameContext() = default;

		[[nodiscard]] virtual uint32_t GetFrameSlotIndex() const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetBackBufferIndex() const noexcept = 0;
		[[nodiscard]] virtual RHITextureHandle GetBackBuffer() const noexcept = 0;
		[[nodiscard]] virtual RHIGraphicsCommandContext& GetGraphicsContext() noexcept = 0;
		[[nodiscard]] virtual RHIComputeCommandContext& GetDirectComputeContext() noexcept = 0;
		[[nodiscard]] virtual RHIComputeCommandContext* GetComputeContext() noexcept = 0;
	};

	enum class RHIFrameBeginStatus : uint8_t
	{
		Ready,
		Unavailable,
		Fatal,
	};

	class RHIFrameBeginResult
	{
	public:
		[[nodiscard]] static RHIFrameBeginResult Ready(RHIFrameContext& frame) noexcept
		{
			return RHIFrameBeginResult(RHIFrameBeginStatus::Ready, &frame);
		}
		[[nodiscard]] static RHIFrameBeginResult Unavailable() noexcept
		{
			return RHIFrameBeginResult(RHIFrameBeginStatus::Unavailable, nullptr);
		}
		[[nodiscard]] static RHIFrameBeginResult Fatal() noexcept
		{
			return RHIFrameBeginResult(RHIFrameBeginStatus::Fatal, nullptr);
		}

		[[nodiscard]] RHIFrameBeginStatus GetStatus() const noexcept { return m_Status; }
		[[nodiscard]] bool IsReady() const noexcept
		{
			return m_Status == RHIFrameBeginStatus::Ready && m_Frame != nullptr;
		}
		[[nodiscard]] bool IsUnavailable() const noexcept
		{
			return m_Status == RHIFrameBeginStatus::Unavailable;
		}
		[[nodiscard]] bool IsFatal() const noexcept
		{
			return m_Status == RHIFrameBeginStatus::Fatal;
		}
		[[nodiscard]] RHIFrameContext* GetFrame() const noexcept { return m_Frame; }

	private:
		RHIFrameBeginResult(RHIFrameBeginStatus status, RHIFrameContext* frame) noexcept :
			m_Status(status), m_Frame(frame)
		{
		}

		RHIFrameBeginStatus m_Status = RHIFrameBeginStatus::Fatal;
		RHIFrameContext* m_Frame = nullptr;
	};

	enum class RHIFrameEndStatus : uint8_t
	{
		Completed,
		Fatal,
	};

	class RHIFrameEndResult
	{
	public:
		[[nodiscard]] static RHIFrameEndResult Completed(
			const RHIFencePoint& submittedFence) noexcept
		{
			GGLAB_ASSERT_MSG(submittedFence.IsValid(),
				"A completed RHI frame transaction requires a submitted fence.");
			return RHIFrameEndResult(RHIFrameEndStatus::Completed, submittedFence);
		}
		[[nodiscard]] static RHIFrameEndResult Fatal(
			const RHIFencePoint& submittedFence = {}) noexcept
		{
			return RHIFrameEndResult(RHIFrameEndStatus::Fatal, submittedFence);
		}

		[[nodiscard]] RHIFrameEndStatus GetStatus() const noexcept { return m_Status; }
		[[nodiscard]] bool IsCompleted() const noexcept
		{
			return m_Status == RHIFrameEndStatus::Completed;
		}
		[[nodiscard]] bool IsFatal() const noexcept
		{
			return m_Status == RHIFrameEndStatus::Fatal;
		}
		[[nodiscard]] bool HasSubmission() const noexcept
		{
			return m_SubmittedFence.IsValid();
		}
		[[nodiscard]] const RHIFencePoint& GetSubmittedFence() const noexcept
		{
			return m_SubmittedFence;
		}

	private:
		RHIFrameEndResult(RHIFrameEndStatus status, const RHIFencePoint& submittedFence) noexcept :
			m_Status(status), m_SubmittedFence(submittedFence)
		{
		}

		RHIFrameEndStatus m_Status = RHIFrameEndStatus::Fatal;
		RHIFencePoint m_SubmittedFence{};
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
		[[nodiscard]] virtual RHIPipelineSystem& GetPipelineSystem() noexcept = 0;
		[[nodiscard]] virtual GpuProfiler* GetGpuProfiler() noexcept = 0;

		[[nodiscard]] virtual RHIFrameBeginResult BeginFrame() noexcept = 0;
		[[nodiscard]] virtual RHIFrameEndResult EndFrame(RHIFrameContext& frame) noexcept = 0;
		[[nodiscard]] virtual RHIFencePoint AbortFrame(RHIFrameContext& frame) noexcept = 0;
		virtual void WaitForFence(
			RHIQueueType waitingQueue, const RHIFencePoint& fencePoint) noexcept = 0;

		virtual void Resize(uint32_t width, uint32_t height) noexcept = 0;
		virtual void WaitIdle() noexcept = 0;
		virtual void RetireCompletedWork() noexcept = 0;

		[[nodiscard]] virtual uint32_t GetFrameSlotCount() const noexcept = 0;
	};

	// Host-owned composition seam for one selected backend/platform pair. Renderer
	// borrows the factory only while CreateContext runs and owns the returned context.
	class RHIContextFactoryBase
	{
	public:
		virtual ~RHIContextFactoryBase();

		[[nodiscard]] virtual std::unique_ptr<RHIContext> CreateContext(
			const RHIContextDesc& desc) const noexcept = 0;
	};
}
