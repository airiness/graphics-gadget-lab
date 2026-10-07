#include "Graphics/Capture/FrameCaptureService.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <iterator>
#include <memory>
#include <utility>

namespace gglab
{
	FrameCaptureService::FrameCaptureService(RHIDevice& device) noexcept :
		m_Device(std::addressof(device))
	{
	}

	FrameCaptureService::~FrameCaptureService()
	{
		GGLAB_ASSERT_MSG(m_Taps.empty(),
			"FrameCaptureService must release its readback buffers before destruction.");
	}

	uint64_t FrameCaptureService::RequestCapture(FrameCaptureSource source) noexcept
	{
		const uint64_t requestId = m_NextRequestId++;
		if (m_IsShutdown)
		{
			PublishFailure(requestId, source, 0, "The render host has been finalized.");
			return requestId;
		}
		m_Queued.push_back({ .m_Id = requestId, .m_Source = source });
		return requestId;
	}

	uint64_t FrameCaptureService::RequestDiagnosticCapture(PostProcessDebugTap tap) noexcept
	{
		const uint64_t requestId = m_NextRequestId++;
		if (m_IsShutdown)
		{
			PublishFailure(requestId, FrameCaptureSource::Diagnostic, 0,
				"The render host has been finalized.");
			return requestId;
		}
		if (GetFrameCaptureDiagnosticTapName(tap).empty())
		{
			PublishFailure(requestId, FrameCaptureSource::Diagnostic, 0,
				"The tap does not support diagnostic captures.");
			return requestId;
		}
		m_Queued.push_back({
			.m_Id = requestId,
			.m_Source = FrameCaptureSource::Diagnostic,
			.m_DiagnosticTap = tap,
			});
		return requestId;
	}

	std::optional<PostProcessDebugTap> FrameCaptureService::GetPendingDiagnosticTap()
		const noexcept
	{
		const auto request = std::ranges::find(
			m_Queued, FrameCaptureSource::Diagnostic, &QueuedRequest::m_Source);
		return request != m_Queued.end() ? std::optional(request->m_DiagnosticTap)
										 : std::nullopt;
	}

	void FrameCaptureService::FailPendingDiagnosticRequests(std::string_view failure) noexcept
	{
		const std::optional<PostProcessDebugTap> tap = GetPendingDiagnosticTap();
		if (!tap)
		{
			return;
		}
		std::erase_if(m_Queued, [&](const QueuedRequest& request)
			{
				if (!IsBoundBy(request, FrameCaptureSource::Diagnostic, *tap))
				{
					return false;
				}
				PublishFailure(request.m_Id, request.m_Source, 0, std::string(failure));
				return true;
			});
	}

	bool FrameCaptureService::IsBoundBy(const QueuedRequest& request, FrameCaptureSource source,
		PostProcessDebugTap diagnosticTap) const noexcept
	{
		return request.m_Source == source && (source != FrameCaptureSource::Diagnostic ||
			request.m_DiagnosticTap == diagnosticTap);
	}

	void FrameCaptureService::ConsumeResults(std::vector<FrameCaptureResult>& outResults) noexcept
	{
		outResults.insert(outResults.end(), std::make_move_iterator(m_Results.begin()),
			std::make_move_iterator(m_Results.end()));
		m_Results.clear();
	}

	uint32_t FrameCaptureService::GetUnfinishedRequestCount() const noexcept
	{
		size_t count = m_Queued.size();
		for (const Tap& tap : m_Taps)
		{
			count += tap.m_RequestIds.size();
		}
		return static_cast<uint32_t>(count);
	}

	bool FrameCaptureService::HasPendingRequests(FrameCaptureSource source) const noexcept
	{
		return std::ranges::any_of(m_Queued,
			[source](const QueuedRequest& request) { return request.m_Source == source; });
	}

	std::optional<FrameCaptureTapTarget> FrameCaptureService::BindTap(uint64_t frameSerial,
		FrameCaptureSource source, const RHITextureDesc& displayTargetDesc) noexcept
	{
		GGLAB_ASSERT_MSG(!m_IsShutdown, "Frame capture taps require a live render host.");
		if (m_IsShutdown || !HasPendingRequests(source))
		{
			return std::nullopt;
		}

		const PostProcessDebugTap diagnosticTap =
			GetPendingDiagnosticTap().value_or(PostProcessDebugTap::SceneColor);
		Tap tap{
			.m_FrameSerial = frameSerial,
			.m_Source = source,
			.m_DiagnosticTap = diagnosticTap,
			.m_Footprint = ComputeRHITextureCopyFootprint(displayTargetDesc.m_Format,
				displayTargetDesc.m_Extent.m_Width, displayTargetDesc.m_Extent.m_Height),
		};
		const auto boundEnd = std::ranges::stable_partition(m_Queued,
			[&](const QueuedRequest& request)
			{
				return !IsBoundBy(request, source, diagnosticTap);
			})
			.begin();
		for (auto request = boundEnd; request != m_Queued.end(); ++request)
		{
			tap.m_RequestIds.push_back(request->m_Id);
		}
		m_Queued.erase(boundEnd, m_Queued.end());

		if (!Test(displayTargetDesc.m_Usage, RHITextureUsage::CopySource))
		{
			FailTap(tap, 0, "The display target does not support copy reads.");
			return std::nullopt;
		}
		if (!tap.m_Footprint.IsValid())
		{
			FailTap(tap, 0, std::format("The display target format '{}' with extent {}x{} has no "
				"copyable footprint.", GetRHIFormatInfo(displayTargetDesc.m_Format).m_Name,
				displayTargetDesc.m_Extent.m_Width, displayTargetDesc.m_Extent.m_Height));
			return std::nullopt;
		}

		const RHIBufferDesc bufferDesc{
			.m_SizeInBytes = tap.m_Footprint.m_SizeInBytes,
			.m_Usage = RHIBufferUsage::CopyDest,
			.m_MemoryUsage = RHIMemoryUsage::GpuToCpu,
			.m_DebugName = "FrameCapture.Readback",
		};
		tap.m_Buffer = RHIBufferOwner(m_Device, m_Device->CreateBuffer(bufferDesc));
		if (!tap.m_Buffer)
		{
			GGLAB_LOG_GRAPHICS_ERROR("Frame capture failed to allocate a {}-byte readback buffer.",
				bufferDesc.m_SizeInBytes);
			FailTap(tap, 0, "The readback buffer could not be allocated.");
			return std::nullopt;
		}

		const FrameCaptureTapTarget target{
			.m_Buffer = tap.m_Buffer.Get(),
			.m_BufferDesc = bufferDesc,
			.m_Footprint = tap.m_Footprint,
		};
		m_Taps.push_back(std::move(tap));
		return target;
	}

	void FrameCaptureService::OnFrameSubmitted(
		uint64_t frameSerial, const RHIFencePoint& fence) noexcept
	{
		GGLAB_ASSERT_MSG(fence.IsValid(), "A submitted frame must provide its completion fence.");
		for (Tap& tap : m_Taps)
		{
			if (tap.m_FrameSerial == frameSerial && !tap.m_Submitted)
			{
				tap.m_Fence = fence;
				tap.m_Submitted = true;
			}
		}
	}

	void FrameCaptureService::OnFrameAborted(uint64_t frameSerial) noexcept
	{
		std::vector<QueuedRequest> returned;
		std::erase_if(m_Taps, [&](const Tap& tap)
			{
				if (tap.m_FrameSerial != frameSerial || tap.m_Submitted)
				{
					return false;
				}
				for (const uint64_t requestId : tap.m_RequestIds)
				{
					returned.push_back({ .m_Id = requestId, .m_Source = tap.m_Source,
						.m_DiagnosticTap = tap.m_DiagnosticTap });
				}
				return true;
			});
		if (returned.empty())
		{
			return;
		}
		// Request ids grow monotonically, so id order is the original queue order.
		m_Queued.insert(m_Queued.end(), returned.begin(), returned.end());
		std::ranges::sort(m_Queued, {}, &QueuedRequest::m_Id);
	}

	void FrameCaptureService::OnFrameSubmissionFailed(uint64_t frameSerial) noexcept
	{
		std::erase_if(m_Taps, [&](Tap& tap)
			{
				if (tap.m_FrameSerial != frameSerial || tap.m_Submitted)
				{
					return false;
				}
				FailTap(tap, frameSerial, "The frame that recorded the capture failed to submit.");
				return true;
			});
	}

	void FrameCaptureService::CollectCompleted() noexcept
	{
		std::erase_if(m_Taps, [this](Tap& tap)
			{
				if (!tap.m_Submitted || !m_Device->IsFencePointCompleted(tap.m_Fence))
				{
					return false;
				}
				PublishTap(tap);
				return true;
			});
	}

	void FrameCaptureService::Shutdown() noexcept
	{
		if (m_IsShutdown)
		{
			return;
		}
		CollectCompleted();
		for (Tap& tap : m_Taps)
		{
			FailTap(tap, tap.m_FrameSerial,
				"The render host was finalized before the capture completed.");
		}
		m_Taps.clear();
		for (const QueuedRequest& request : m_Queued)
		{
			PublishFailure(request.m_Id, request.m_Source, 0,
				"The render host was finalized before a frame recorded the capture.");
		}
		m_Queued.clear();
		m_IsShutdown = true;
	}

	void FrameCaptureService::PublishFailure(uint64_t requestId, FrameCaptureSource source,
		uint64_t frameSerial, std::string failure) noexcept
	{
		m_Results.push_back({
			.m_RequestId = requestId,
			.m_Source = source,
			.m_Status = FrameCaptureStatus::Failed,
			.m_FrameSerial = frameSerial,
			.m_Failure = std::move(failure),
			});
	}

	void FrameCaptureService::FailTap(
		Tap& tap, uint64_t frameSerial, std::string_view failure) noexcept
	{
		for (const uint64_t requestId : tap.m_RequestIds)
		{
			PublishFailure(requestId, tap.m_Source, frameSerial, std::string(failure));
		}
		tap.m_RequestIds.clear();
		tap.m_Buffer.Reset();
	}

	void FrameCaptureService::PublishTap(Tap& tap) noexcept
	{
		const RHITextureCopyFootprint& footprint = tap.m_Footprint;
		const RHIBufferHandle buffer = tap.m_Buffer.Get();
		const auto* mapped = static_cast<const std::byte*>(m_Device->MapBuffer(buffer, {
			.m_Begin = 0,
			.m_End = footprint.m_SizeInBytes,
			}));
		if (!mapped)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Frame capture failed to map the readback of frame {}.", tap.m_FrameSerial);
			FailTap(tap, tap.m_FrameSerial, "The completed readback buffer could not be mapped.");
			return;
		}

		auto image = std::make_shared<FrameCaptureImage>();
		image->m_Format = footprint.m_Format;
		image->m_Width = footprint.m_Width;
		image->m_Height = footprint.m_Height;
		image->m_Pixels.resize(
			static_cast<size_t>(footprint.m_RowSizeInBytes) * footprint.m_Height);
		for (uint32_t row = 0; row < footprint.m_Height; ++row)
		{
			std::memcpy(image->m_Pixels.data() + row * footprint.m_RowSizeInBytes,
				mapped + row * footprint.m_RowPitch, footprint.m_RowSizeInBytes);
		}
		m_Device->UnmapBuffer(buffer, {});

		std::shared_ptr<const FrameCaptureImage> sharedImage = std::move(image);
		for (const uint64_t requestId : tap.m_RequestIds)
		{
			m_Results.push_back({
				.m_RequestId = requestId,
				.m_Source = tap.m_Source,
				.m_Status = FrameCaptureStatus::Completed,
				.m_FrameSerial = tap.m_FrameSerial,
				.m_Image = sharedImage,
				});
		}
		tap.m_RequestIds.clear();
		tap.m_Buffer.Reset();
	}
}
