#include "Graphics/Capture/FrameCaptureService.h"
#include "Graphics/Capture/CaptureProduct.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <memory>
#include <span>
#include <utility>

namespace gglab
{
	FrameCaptureService::FrameCaptureService(RHIDevice& device, FrameCaptureLimits limits) noexcept :
		m_Device(std::addressof(device)), m_Limits(limits)
	{
	}

	FrameCaptureService::~FrameCaptureService()
	{
		GGLAB_ASSERT_MSG(m_Taps.empty(),
			"FrameCaptureService must release its readback buffers before destruction.");
	}

	uint64_t FrameCaptureService::RequestCapture(
		FrameCaptureSource source, uint64_t frameSerial, uint64_t groupId) noexcept
	{
		if (GetUnfinishedRequestCount() + m_Results.size() >= m_Limits.m_MaxRequests)
		{
			return 0;
		}
		const uint64_t requestId = m_NextRequestId++;
		if (m_IsShutdown)
		{
			PublishFailure(requestId, source, 0, "The render host has been finalized.");
			return requestId;
		}
		if (source == FrameCaptureSource::NonFiniteScan)
		{
			PublishFailure(requestId, source, 0, "Use RequestNonFiniteScan with explicit targets and a configuration version.");
			return requestId;
		}
		m_Queued.push_back({ .m_Id = requestId, .m_Source = source, .m_FrameSerial = frameSerial, .m_GroupId = groupId });
		DeferUnrelatedImages(frameSerial);
		return requestId;
	}

	uint64_t FrameCaptureService::RequestDiagnosticCapture(
		PostProcessDebugTap tap, uint64_t frameSerial, uint64_t groupId) noexcept
	{
		if (GetUnfinishedRequestCount() + m_Results.size() >= m_Limits.m_MaxRequests)
		{
			return 0;
		}
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
			.m_FrameSerial = frameSerial,
			.m_GroupId = groupId,
			.m_DiagnosticTap = tap,
			});
		DeferUnrelatedImages(frameSerial);
		return requestId;
	}

	std::optional<PostProcessDebugTap> FrameCaptureService::GetPendingDiagnosticTap()
		const noexcept
	{
		const auto request = std::ranges::find(
			m_Queued, FrameCaptureSource::Diagnostic, &QueuedRequest::m_Source);
		return request != m_Queued.end() ?
			std::optional(request->m_DiagnosticTap) :
			std::nullopt;
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
				PublishFailure(request.m_Id, request.m_Source, 0, std::string(failure),
					request.m_DiagnosticTap);
				return true;
			});
	}

	bool FrameCaptureService::IsBoundBy(const QueuedRequest& request, FrameCaptureSource source,
		PostProcessDebugTap diagnosticTap) const noexcept
	{
		return request.m_Source == source && (source != FrameCaptureSource::Diagnostic ||
			request.m_DiagnosticTap == diagnosticTap);
	}

	void FrameCaptureService::DeferUnrelatedImages(uint64_t frameSerial) noexcept
	{
		const auto scheduledExport = std::ranges::find_if(m_Queued, [&](const QueuedRequest& request)
			{
				return request.m_Source == FrameCaptureSource::ResourceExport && request.m_DeferFrameOnBusy &&
					(frameSerial == 0 || request.m_FrameSerial == 0 || request.m_FrameSerial == frameSerial);
			});
		if (scheduledExport == m_Queued.end())
		{
			return;
		}
		const uint64_t groupId = scheduledExport->m_GroupId;
		const uint64_t selectedFrame = frameSerial != 0 ? frameSerial : scheduledExport->m_FrameSerial;
		std::vector<QueuedRequest> requiredImages;
		for (const auto& request : m_Queued)
		{
			if (groupId != 0 && request.m_GroupId == groupId &&
				request.m_Source != FrameCaptureSource::ResourceExport && request.m_Source != FrameCaptureSource::RenderGraph &&
				request.m_Source != FrameCaptureSource::NonFiniteScan &&
				(request.m_FrameSerial == 0 || request.m_FrameSerial == selectedFrame))
			{
				requiredImages.push_back(request);
			}
		}
		// Delay unrelated images before diagnostic generation as well as copy-pass
		// construction. An unavailable diagnostic feature on this frame must not
		// turn temporary scheduling pressure into an unrelated capture failure.
		std::erase_if(m_Queued, [&](const QueuedRequest& request)
			{
				if (request.m_Source == FrameCaptureSource::ResourceExport || request.m_Source == FrameCaptureSource::RenderGraph ||
					request.m_Source == FrameCaptureSource::NonFiniteScan ||
					(request.m_FrameSerial != 0 && request.m_FrameSerial != selectedFrame) ||
					std::ranges::any_of(requiredImages, [&](const QueuedRequest& required)
						{
							return IsBoundBy(request, required.m_Source, required.m_DiagnosticTap);
						}))
				{
					return false;
				}
				m_Results.push_back({ .m_RequestId = request.m_Id, .m_Source = request.m_Source,
					.m_Status = FrameCaptureStatus::Busy, .m_FrameSerial = selectedFrame,
					.m_DiagnosticTap = request.m_Source == FrameCaptureSource::Diagnostic ? std::optional(request.m_DiagnosticTap) : std::nullopt });
				return true;
			});
	}

	void FrameCaptureService::ConsumeResults(std::vector<FrameCaptureResult>& outResults) noexcept
	{
		// A backpressured sequence may stop calling BeginFrame. Keep fence retirement
		// progressing on the same frame-driving thread while it polls for capacity.
		if (!m_IsShutdown)
		{
			CollectCompleted();
		}
		outResults.insert(outResults.end(), std::make_move_iterator(m_Results.begin()),
			std::make_move_iterator(m_Results.end()));
		m_Results.clear();
	}

	uint32_t FrameCaptureService::GetUnfinishedRequestCount() const noexcept
	{
		size_t count = m_Queued.size();
		for (const Tap& tap : m_Taps)
		{
			count += tap.m_Requests.size();
		}
		return static_cast<uint32_t>(count);
	}

	FrameCaptureAvailability FrameCaptureService::GetRequestAvailability(uint32_t requestCount) const noexcept
	{
		if (m_IsShutdown)
		{
			return FrameCaptureAvailability::Finalized;
		}
		if (requestCount > m_Limits.m_MaxRequests)
		{
			return FrameCaptureAvailability::TooLarge;
		}
		return GetUnfinishedRequestCount() + m_Results.size() > m_Limits.m_MaxRequests - requestCount ?
			FrameCaptureAvailability::Busy :
			FrameCaptureAvailability::Available;
	}

	FrameCaptureAvailability FrameCaptureService::GetAvailability(
		RHIFormat format, uint32_t width, uint32_t height) const noexcept
	{
		if (m_IsShutdown)
		{
			return FrameCaptureAvailability::Finalized;
		}
		const RHITextureCopyFootprint footprint = ComputeRHITextureCopyFootprint(format, width, height);
		if (!footprint.IsValid())
		{
			return FrameCaptureAvailability::Unsupported;
		}
		// Check multiplication and addition against the budget before using their
		// results, including extreme extents whose footprint byte count could wrap.
		if (footprint.m_RowPitch > m_Limits.m_MaxPayloadBytes / height)
		{
			return FrameCaptureAvailability::TooLarge;
		}
		const uint64_t imageBytes = footprint.m_RowSizeInBytes * height;
		if (imageBytes > m_Limits.m_MaxPayloadBytes - footprint.m_SizeInBytes)
		{
			return FrameCaptureAvailability::TooLarge;
		}
		const uint64_t requiredBytes = footprint.m_SizeInBytes + imageBytes;
		const auto [usedBytes, payloads] = GetPayloadUsage();
		// Failure reports can retain CPU credit beside unread GPU work. Saturation
		// must not wrap remaining-byte arithmetic or authorize another allocation.
		return payloads >= m_Limits.m_MaxPayloads ||
			GetUnfinishedRequestCount() + m_Results.size() >= m_Limits.m_MaxRequests ||
			usedBytes > m_Limits.m_MaxPayloadBytes ||
			requiredBytes > m_Limits.m_MaxPayloadBytes - usedBytes ?
				FrameCaptureAvailability::Busy :
				FrameCaptureAvailability::Available;
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
		DeferUnrelatedImages(frameSerial);

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
				return !IsBoundBy(request, source, diagnosticTap) ||
					(request.m_FrameSerial != 0 && request.m_FrameSerial != frameSerial);
			})
			.begin();
		for (auto request = boundEnd; request != m_Queued.end(); ++request)
		{
			tap.m_Requests.push_back(*request);
		}
		m_Queued.erase(boundEnd, m_Queued.end());
		if (tap.m_Requests.empty())
		{
			return std::nullopt;
		}

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
		const FrameCaptureAvailability availability = GetAvailability(displayTargetDesc.m_Format,
			displayTargetDesc.m_Extent.m_Width, displayTargetDesc.m_Extent.m_Height);
		if (availability != FrameCaptureAvailability::Available)
		{
			FailTap(tap, 0, availability == FrameCaptureAvailability::TooLarge ?
				"The capture target exceeds the payload byte budget." :
				"Frame capture payload capacity is exhausted.");
			return std::nullopt;
		}
		// Only this owner admits work. Concurrent CPU consumers can only release credit
		// between the aggregate availability check and this reservation.
		tap.m_PayloadLease = m_PayloadBudget.Reserve(tap.m_Footprint.m_SizeInBytes +
			tap.m_Footprint.m_RowSizeInBytes * tap.m_Footprint.m_Height, 1);

		const RHIBufferDesc bufferDesc{
			.m_SizeInBytes = tap.m_Footprint.m_SizeInBytes,
			.m_Usage = RHIBufferUsage::CopyDest,
			.m_MemoryUsage = RHIMemoryUsage::GpuToCpu,
			.m_DebugName = "FrameCapture.Readback",
		};
		tap.m_Readback.SetBuffer(RHIBufferOwner(m_Device, m_Device->CreateBuffer(bufferDesc)));
		if (!tap.m_Readback.GetBuffer().IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR("Frame capture failed to allocate a {}-byte readback buffer.",
				bufferDesc.m_SizeInBytes);
			FailTap(tap, 0, "The readback buffer could not be allocated.");
			return std::nullopt;
		}

		tap.m_AdapterCompatibilityIdentity = m_Device->GetAdapterCompatibilityIdentity();
		const FrameCaptureTapTarget target{
			.m_Buffer = tap.m_Readback.GetBuffer(),
			.m_BufferDesc = bufferDesc,
			.m_Footprint = tap.m_Footprint,
		};
		m_Taps.push_back(std::move(tap));
		return target;
	}

	void FrameCaptureService::BeginGraphRecording(uint64_t frameSerial, RenderGraph& graph) noexcept
	{
		const bool includeGraph = std::ranges::any_of(m_Taps, [&](const Tap& tap)
			{
				return tap.m_FrameSerial == frameSerial && (tap.m_Source == FrameCaptureSource::ResourceExport || tap.m_Source == FrameCaptureSource::NonFiniteScan) &&
					std::ranges::any_of(tap.m_Requests, [](const auto& request) { return request.m_IncludeGraph; });
			});
		if (m_IsShutdown || (!HasPendingRequests(FrameCaptureSource::RenderGraph) && !includeGraph))
		{
			return;
		}
		Tap tap{ .m_FrameSerial = frameSerial, .m_Source = FrameCaptureSource::RenderGraph };
		std::erase_if(m_Queued, [&](const QueuedRequest& request)
			{
				if (request.m_Source != FrameCaptureSource::RenderGraph ||
					(request.m_FrameSerial != 0 && request.m_FrameSerial != frameSerial))
				{
					return false;
				}
				tap.m_Requests.push_back(request);
				return true;
			});
		if (tap.m_Requests.empty() && !includeGraph)
		{
			return;
		}
		tap.m_Graph = std::make_shared<GraphCaptureProducer>(graph);
		tap.m_AdapterCompatibilityIdentity = m_Device->GetAdapterCompatibilityIdentity();
		tap.m_ResourceExportActive = graph.HasResourceObservations();
		tap.m_NonFiniteScanActive = graph.HasNonFiniteScans();
		tap.m_GraphExportActive = true;
		for (auto& consumer : m_Taps)
		{
			if (consumer.m_FrameSerial == frameSerial)
			{
				consumer.m_GraphExportActive = true;
			}
		}
		for (auto& resources : m_Taps)
		{
			if (resources.m_FrameSerial == frameSerial && (resources.m_Source == FrameCaptureSource::ResourceExport || resources.m_Source == FrameCaptureSource::NonFiniteScan) &&
				!resources.m_Requests.empty() && resources.m_Requests[0].m_IncludeGraph)
			{
				resources.m_Graph = tap.m_Graph;
			}
		}
		if (!tap.m_Requests.empty())
		{
			m_Taps.push_back(std::move(tap));
		}
	}

	void FrameCaptureService::EndGraphRecording(uint64_t frameSerial, RenderGraph& graph) noexcept
	{
		if (!std::ranges::any_of(m_Taps, [&](const Tap& tap)
			{
				return tap.m_FrameSerial == frameSerial && tap.m_Graph && !tap.m_Readback.IsSubmitted();
			}))
		{
			return;
		}
		auto record = graph.TakeExecutionRecord();
		if (!record)
		{
			return;
		}
		for (Tap& tap : m_Taps)
		{
			if (tap.m_FrameSerial == frameSerial && tap.m_Graph && !tap.m_Readback.IsSubmitted())
			{
				tap.m_Graph->Finish(std::move(*record));
				break; // All graph consumers of this frame share this immutable snapshot.
			}
		}
	}

	void FrameCaptureService::OnFrameSubmitted(
		uint64_t frameSerial, const RHIFencePoint& fence) noexcept
	{
		GGLAB_ASSERT_MSG(fence.IsValid(), "A submitted frame must provide its completion fence.");
		for (Tap& tap : m_Taps)
		{
			if (tap.m_FrameSerial == frameSerial && !tap.m_Readback.IsSubmitted())
			{
				tap.m_Readback.Submit(fence);
			}
		}
		FailQueuedFrameRequests(frameSerial, "The requested frame did not record its capture tap.");
	}

	void FrameCaptureService::OnFrameAborted(uint64_t frameSerial) noexcept
	{
		std::vector<QueuedRequest> returned;
		std::erase_if(m_Taps, [&](const Tap& tap)
			{
				if (tap.m_FrameSerial != frameSerial || tap.m_Readback.IsSubmitted())
				{
					return false;
				}
				for (const QueuedRequest& request : tap.m_Requests)
				{
					if (request.m_FrameSerial == 0)
					{
						returned.push_back(request);
					}
					else
					{
						PublishFailure(request.m_Id, request.m_Source, frameSerial,
							"The requested capture frame was aborted before submission.",
							request.m_Source == FrameCaptureSource::Diagnostic ?
								std::optional(request.m_DiagnosticTap) :
								std::nullopt);
					}
				}
				return true;
			});
		FailQueuedFrameRequests(frameSerial, "The requested capture frame was aborted before submission.");
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
				if (tap.m_FrameSerial != frameSerial || tap.m_Readback.IsSubmitted())
				{
					return false;
				}
				FailTap(tap, frameSerial, "The frame that recorded the capture failed to submit.");
				return true;
			});
		FailQueuedFrameRequests(frameSerial, "The requested capture frame failed to submit.");
	}

	void FrameCaptureService::OnFrameCompletionUnknown(uint64_t frameSerial) noexcept
	{
		for (Tap& tap : m_Taps)
		{
			if (tap.m_FrameSerial == frameSerial && !tap.m_Readback.IsSubmitted() && !tap.m_Readback.IsCompletionUnknown())
			{
				PublishTapFailure(tap, frameSerial,
					"The GPU completion of the frame that recorded the capture is unknown.");
				tap.m_Readback.MarkCompletionUnknown();
			}
		}
		FailQueuedFrameRequests(
			frameSerial, "The GPU completion of the requested capture frame is unknown.");
	}

	void FrameCaptureService::CollectCompleted() noexcept
	{
		std::erase_if(m_Taps, [this](Tap& tap)
			{
				if (!tap.m_Readback.IsSubmitted() || tap.m_Readback.IsCompletionUnknown())
				{
					return false;
				}
				// Only a completed fence certifies the readback contents. A lost device allows
				// releasing the readbacks, never reading them.
				switch (tap.m_Readback.Poll(*m_Device))
				{
				case RHIFenceQueryResult::Pending:
					return false;
				case RHIFenceQueryResult::Completed:
					PublishTap(tap);
					return true;
				case RHIFenceQueryResult::DeviceLost:
					FailTap(tap, tap.m_FrameSerial,
						"The device was lost before the capture completed.");
					return true;
				case RHIFenceQueryResult::Failed:
					break;
				}
				// A failed query proves nothing about the copy, so the readbacks stay unread
				// until teardown.
				PublishTapFailure(tap, tap.m_FrameSerial,
					"The completion of the frame that recorded the capture could not be queried.");
				tap.m_Readback.MarkCompletionUnknown();
				return false;
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
			if (!tap.m_Readback.IsCompletionUnknown())
			{
				FailTap(tap, tap.m_FrameSerial,
					"The render host was finalized before the capture completed.");
			}
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

	void FrameCaptureService::Abandon(std::string_view failure) noexcept
	{
		if (m_IsShutdown)
		{
			return;
		}
		for (Tap& tap : m_Taps)
		{
			if (!tap.m_Readback.IsCompletionUnknown())
			{
				PublishTapFailure(tap, tap.m_FrameSerial, failure);
			}
		}
		// The GPU may still write the readbacks, so their owners are leaked, never destroyed.
		auto* abandonedTaps = new std::vector<Tap>(std::move(m_Taps));
		GGLAB_UNUSED(abandonedTaps);
		m_Taps.clear();
		for (const QueuedRequest& request : m_Queued)
		{
			PublishFailure(request.m_Id, request.m_Source, 0, std::string(failure));
		}
		m_Queued.clear();
		m_IsShutdown = true;
	}

	void FrameCaptureService::PublishFailure(uint64_t requestId, FrameCaptureSource source,
		uint64_t frameSerial, std::string failure,
		std::optional<PostProcessDebugTap> diagnosticTap) noexcept
	{
		m_Results.push_back({
			.m_RequestId = requestId,
			.m_Source = source,
			.m_Status = FrameCaptureStatus::Failed,
			.m_FrameSerial = frameSerial,
			.m_DiagnosticTap = diagnosticTap,
			.m_Failure = std::move(failure),
			});
	}

	void FrameCaptureService::FailQueuedFrameRequests(
		uint64_t frameSerial, std::string_view failure) noexcept
	{
		std::erase_if(m_Queued, [&](const QueuedRequest& request)
			{
				if (request.m_FrameSerial == 0 || request.m_FrameSerial > frameSerial)
				{
					return false;
				}
				PublishFailure(request.m_Id, request.m_Source, 0, std::string(failure),
					request.m_Source == FrameCaptureSource::Diagnostic ?
						std::optional(request.m_DiagnosticTap) :
						std::nullopt);
				return true;
			});
	}

	void FrameCaptureService::FailTap(
		Tap& tap, uint64_t frameSerial, std::string_view failure) noexcept
	{
		PublishTapFailure(tap, frameSerial, failure);
		tap.m_Readback.ReleaseBuffers();
		tap.m_PayloadLease.Reset();
	}

	void FrameCaptureService::PublishTapFailure(
		Tap& tap, uint64_t frameSerial, std::string_view failure) noexcept
	{
		if (tap.m_ProductionCounters)
		{
			NonFiniteProductionFrame frame;
			frame.m_FrameSerial = frameSerial;
			frame.m_Failure = failure;
			// Failure reports retain the existing CPU charge independently of unknown GPU work.
			PublishProductionFrame(MakeCaptureProduct(std::move(frame),
				m_ProductionBudget.Reserve(ProductionCaptureProducer::CpuBytes, 1)));
		}
		for (const QueuedRequest& request : tap.m_Requests)
		{
			PublishFailure(request.m_Id, tap.m_Source, frameSerial, std::string(failure),
				tap.m_Source == FrameCaptureSource::Diagnostic ?
					std::optional(tap.m_DiagnosticTap) :
					std::nullopt);
		}
		tap.m_Requests.clear();
	}

	void FrameCaptureService::PublishTap(Tap& tap) noexcept
	{
		if (tap.m_ProductionCounters)
		{
			PublishProductionTap(tap);
			return;
		}
		if (tap.m_Source == FrameCaptureSource::NonFiniteScan)
		{
			PublishScanTap(tap);
			return;
		}
		if (tap.m_Source == FrameCaptureSource::ResourceExport)
		{
			PublishResourceTap(tap);
			return;
		}
		if (tap.m_Graph)
		{
			if (!tap.m_Graph->IsRecorded())
			{
				FailTap(tap, tap.m_FrameSerial, "The requested graph did not execute.");
				return;
			}
			for (const QueuedRequest& request : tap.m_Requests)
			{
				m_Results.push_back({
					.m_RequestId = request.m_Id,
					.m_Source = tap.m_Source,
					.m_Status = FrameCaptureStatus::Completed,
					.m_FrameSerial = tap.m_FrameSerial,
					.m_Graph = tap.m_Graph ? tap.m_Graph->GetProduct() : nullptr,
					.m_GraphExportActive = tap.m_GraphExportActive,
					.m_ResourceExportActive = tap.m_ResourceExportActive,
					.m_NonFiniteScanActive = tap.m_NonFiniteScanActive,
					.m_SubmissionFenceIndex = tap.m_Readback.GetFence().m_Fence.Index(),
					.m_SubmissionFenceGeneration = tap.m_Readback.GetFence().m_Fence.Generation(),
					.m_SubmissionFenceValue = tap.m_Readback.GetFence().m_Value,
					.m_AdapterCompatibilityIdentity = tap.m_AdapterCompatibilityIdentity,
					});
			}
			tap.m_Requests.clear();
			return;
		}
		const RHITextureCopyFootprint& footprint = tap.m_Footprint;
		FrameCaptureImage image;
		image.m_Format = footprint.m_Format;
		image.m_Width = footprint.m_Width;
		image.m_Height = footprint.m_Height;
		image.m_Pixels.resize(
			static_cast<size_t>(footprint.m_RowSizeInBytes) * footprint.m_Height);
		if (!tap.m_Readback.CopyRows(*m_Device, footprint,
			std::as_writable_bytes(std::span(image.m_Pixels))))
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Frame capture failed to map the readback of frame {}.", tap.m_FrameSerial);
			FailTap(tap, tap.m_FrameSerial, "The completed readback buffer could not be mapped.");
			return;
		}
		tap.m_Readback.ReleaseBuffers();
		tap.m_PayloadLease.ReduceTo(image.m_Pixels.size());
		const auto sharedImage = MakeCaptureProduct(std::move(image), std::move(tap.m_PayloadLease));
		for (const QueuedRequest& request : tap.m_Requests)
		{
			m_Results.push_back({
				.m_RequestId = request.m_Id,
				.m_Source = tap.m_Source,
				.m_Status = FrameCaptureStatus::Completed,
				.m_FrameSerial = tap.m_FrameSerial,
				.m_DiagnosticTap = tap.m_Source == FrameCaptureSource::Diagnostic ?
					std::optional(tap.m_DiagnosticTap) :
					std::nullopt,
				.m_Image = sharedImage,
				.m_GraphExportActive = tap.m_GraphExportActive,
				.m_ResourceExportActive = tap.m_ResourceExportActive,
				.m_NonFiniteScanActive = tap.m_NonFiniteScanActive,
				.m_SubmissionFenceIndex = tap.m_Readback.GetFence().m_Fence.Index(),
				.m_SubmissionFenceGeneration = tap.m_Readback.GetFence().m_Fence.Generation(),
				.m_SubmissionFenceValue = tap.m_Readback.GetFence().m_Value,
				.m_AdapterCompatibilityIdentity = tap.m_AdapterCompatibilityIdentity,
				});
		}
		tap.m_Requests.clear();
	}
}
