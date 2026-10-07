#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureAccess.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIResource.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	class RHIDevice;

	// Owns capture requests, per-frame readback buffers and finished results. The
	// render host drives the frame lifecycle: a tap is bound while the frame graph
	// is built, adopts the frame's submission fence, and keeps its readback buffer
	// until that fence completes and the rows have been copied to CPU memory.
	class FrameCaptureService final : public FrameCaptureControlBase,
		public RenderFrameCaptureAccess
	{
	public:
		explicit FrameCaptureService(RHIDevice& device) noexcept;
		~FrameCaptureService() override;
		GGLAB_DELETE_COPYABLE_MOVABLE(FrameCaptureService);

		[[nodiscard]] uint64_t RequestCapture(FrameCaptureSource source) noexcept override;
		[[nodiscard]] uint64_t RequestDiagnosticCapture(PostProcessDebugTap tap) noexcept override;
		void ConsumeResults(std::vector<FrameCaptureResult>& outResults) noexcept override;
		[[nodiscard]] uint32_t GetUnfinishedRequestCount() const noexcept override;

		[[nodiscard]] bool HasPendingRequests(FrameCaptureSource source) const noexcept override;
		[[nodiscard]] std::optional<FrameCaptureTapTarget> BindTap(uint64_t frameSerial,
			FrameCaptureSource source, const RHITextureDesc& displayTargetDesc) noexcept override;
		[[nodiscard]] std::optional<PostProcessDebugTap> GetPendingDiagnosticTap()
			const noexcept override;
		void FailPendingDiagnosticRequests(std::string_view failure) noexcept override;

		// The frame's GPU work, including its tap copies, was submitted.
		void OnFrameSubmitted(uint64_t frameSerial, const RHIFencePoint& fence) noexcept;
		// The frame ended without recording a submission. Its requests return to
		// the queue in their original order.
		void OnFrameAborted(uint64_t frameSerial) noexcept;
		// The frame's submission failed; its requests finish as Failed.
		void OnFrameSubmissionFailed(uint64_t frameSerial) noexcept;
		// Publishes every submitted tap whose fence has completed.
		void CollectCompleted() noexcept;
		// Requires the device to be idle. Publishes completed taps, fails every
		// remaining request and releases all readback buffers. Later requests fail
		// immediately.
		void Shutdown() noexcept;

	private:
		struct QueuedRequest
		{
			uint64_t m_Id = 0;
			FrameCaptureSource m_Source = FrameCaptureSource::Scene;
			// Diagnostic requests only.
			PostProcessDebugTap m_DiagnosticTap = PostProcessDebugTap::SceneColor;
		};

		struct Tap
		{
			uint64_t m_FrameSerial = 0;
			FrameCaptureSource m_Source = FrameCaptureSource::Scene;
			PostProcessDebugTap m_DiagnosticTap = PostProcessDebugTap::SceneColor;
			std::vector<uint64_t> m_RequestIds;
			RHIBufferOwner m_Buffer;
			RHITextureCopyFootprint m_Footprint{};
			RHIFencePoint m_Fence{};
			bool m_Submitted = false;
		};

		[[nodiscard]] bool IsBoundBy(const QueuedRequest& request, FrameCaptureSource source,
			PostProcessDebugTap diagnosticTap) const noexcept;
		void PublishFailure(uint64_t requestId, FrameCaptureSource source,
			uint64_t frameSerial, std::string failure) noexcept;
		void FailTap(Tap& tap, uint64_t frameSerial, std::string_view failure) noexcept;
		void PublishTap(Tap& tap) noexcept;

		RHIDevice* m_Device = nullptr;
		uint64_t m_NextRequestId = 1;
		std::vector<QueuedRequest> m_Queued;
		std::vector<Tap> m_Taps;
		std::vector<FrameCaptureResult> m_Results;
		bool m_IsShutdown = false;
	};
}
