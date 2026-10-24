#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIDeviceIdentity.h"
#include "GGLabRuntime/Graphics/RHI/RHISwapChain.h"
#include "GGLabRuntime/Graphics/RHI/RHIUpload.h"

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
		bool m_EnableGpuBasedValidation = false;
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
		// Uploads into this frame's submission batch. The value is valid from the Ready
		// result of BeginFrame until this frame's EndFrame or AbortFrame returns and must not
		// be stored across frames.
		[[nodiscard]] virtual RHIFrameUploads GetUploads() noexcept = 0;
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

	// What the frame's own work may still be doing on the GPU when the transaction ends.
	enum class RHIFrameSubmissionState : uint8_t
	{
		// No stream had an effect, or every possibly executed stream was verified idle.
		NoOutstandingWork,
		// The submitted fence completes after every stream that may read frame resources.
		Fenced,
		// Work may still execute and its completion could not be verified. Consumers must
		// not run their unsubmitted cleanup; the context is poisoned.
		CompletionUnknown,
	};

	class RHIFrameEndResult
	{
	public:
		[[nodiscard]] static RHIFrameEndResult Completed(
			const RHIFencePoint& submittedFence) noexcept
		{
			GGLAB_ASSERT_MSG(submittedFence.IsValid(),
				"A completed RHI frame transaction requires a submitted fence.");
			return RHIFrameEndResult(RHIFrameEndStatus::Completed, submittedFence,
				RHIFrameSubmissionState::Fenced);
		}
		// A fatal result with a fence is Fenced; without one, nothing may still execute.
		[[nodiscard]] static RHIFrameEndResult Fatal(
			const RHIFencePoint& submittedFence = {}) noexcept
		{
			return RHIFrameEndResult(RHIFrameEndStatus::Fatal, submittedFence,
				submittedFence.IsValid() ?
					RHIFrameSubmissionState::Fenced :
					RHIFrameSubmissionState::NoOutstandingWork);
		}
		[[nodiscard]] static RHIFrameEndResult FatalCompletionUnknown() noexcept
		{
			return RHIFrameEndResult(
				RHIFrameEndStatus::Fatal, {}, RHIFrameSubmissionState::CompletionUnknown);
		}

		[[nodiscard]] RHIFrameEndStatus GetStatus() const noexcept { return m_Status; }
		[[nodiscard]] RHIFrameSubmissionState GetSubmissionState() const noexcept
		{
			return m_SubmissionState;
		}
		[[nodiscard]] bool IsCompletionUnknown() const noexcept
		{
			return m_SubmissionState == RHIFrameSubmissionState::CompletionUnknown;
		}
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
		RHIFrameEndResult(RHIFrameEndStatus status, const RHIFencePoint& submittedFence,
			RHIFrameSubmissionState submissionState) noexcept :
			m_Status(status), m_SubmittedFence(submittedFence), m_SubmissionState(submissionState)
		{
		}

		RHIFrameEndStatus m_Status = RHIFrameEndStatus::Fatal;
		RHIFencePoint m_SubmittedFence{};
		RHIFrameSubmissionState m_SubmissionState = RHIFrameSubmissionState::NoOutstandingWork;
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
		// Submits the frame's own streams. After a recording or upload-publication failure a
		// backend may abort them instead; the result then reports that abort as AbortFrame would.
		[[nodiscard]] virtual RHIFrameEndResult EndFrame(RHIFrameContext& frame) noexcept = 0;
		// Discards the frame's own streams. The result still reports any backend work the
		// abort itself submitted.
		[[nodiscard]] virtual RHIFrameEndResult AbortFrame(RHIFrameContext& frame) noexcept = 0;
		// GPU-side queue wait. False when the wait could not be issued; work submitted
		// afterwards on the waiting queue is then not ordered after the fence.
		[[nodiscard]] virtual bool WaitForFence(
			RHIQueueType waitingQueue, const RHIFencePoint& fencePoint) noexcept = 0;

		virtual void Resize(uint32_t width, uint32_t height) noexcept = 0;
		// Waits until every queue is idle. Completed is verified idle; it also releases
		// work retained after an unknown frame completion.
		RHIGpuWaitResult WaitIdle() noexcept { return WaitIdleFor(RHIInfiniteWaitMilliseconds); }
		virtual RHIGpuWaitResult WaitIdleFor(uint32_t timeoutMilliseconds) noexcept = 0;
		virtual void RetireCompletedWork() noexcept = 0;
		// Run state: a poisoned context accepts no further frames. Poisoning is not device
		// loss; it does not stop GPU work that was already submitted.
		[[nodiscard]] virtual bool IsPoisoned() const noexcept = 0;
		// Device state: lost only when the native API confirms removal or loss.
		[[nodiscard]] virtual bool IsDeviceLost() const noexcept = 0;

		[[nodiscard]] virtual uint32_t GetFrameSlotCount() const noexcept = 0;
		[[nodiscard]] virtual RHIUploadStatistics GetUploadStatistics() const noexcept { return {}; }
		// Fixed once the device exists. Contexts without a native adapter report it unavailable.
		[[nodiscard]] virtual RHIDeviceIdentity GetDeviceIdentity() const noexcept { return {}; }
	};

	// What teardown may do with GPU-visible resources once no further frames are produced.
	enum class RHITeardownState : uint8_t
	{
		// Every queue was verified idle: owners release through their normal paths.
		Quiesced,
		// Loss is natively confirmed: owners may destroy without fence waits, but never read
		// GPU-written contents.
		DeviceLost,
		// The device is live and idle could not be verified within the bound. GPU-visible
		// memory must be leaked, never destroyed.
		Unverified,
	};

	struct RHITeardownWaitPolicy
	{
		uint32_t m_AttemptCount = 3;
		uint32_t m_AttemptTimeoutMilliseconds = 10'000;
	};

	// Bounded checked idle waits for teardown. The device state is read again after every
	// attempt; a timeout or a failed wait never implies device loss.
	[[nodiscard]] RHITeardownState QuiesceRHIForTeardown(
		RHIContext& context, const RHITeardownWaitPolicy& policy = {}) noexcept;

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
