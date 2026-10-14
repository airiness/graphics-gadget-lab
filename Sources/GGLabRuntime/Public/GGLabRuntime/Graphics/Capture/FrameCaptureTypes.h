#pragma once
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"
#include "GGLabRuntime/Graphics/Capture/ResourceExportTypes.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	struct RGSnapshot;
	// RenderGraph position whose display-target contents a capture reads.
	enum class FrameCaptureSource : uint8_t
	{
		// Display-encoded scene after post-processing, before back-buffer
		// previews, always-visible debug overlays and tooling overlays. Depth-tested
		// debug geometry is part of the scene when its channels are enabled.
		Scene,
		// Final display-target contents immediately before presentation.
		Composited,
		// Display-resolution visualization of one diagnostic tap of the frame, in the
		// encoding of the post-process preview, recorded after post-processing. Each
		// request names its tap; a tap without a source in that frame fails.
		Diagnostic,
		// CPU compile snapshot and execution record; no display tap or GPU readback.
		RenderGraph,
		ResourceExport,
	};

	enum class FrameCaptureStatus : uint8_t
	{
		Completed,
		Failed,
		// Deferred before GPU submission; coordination retries with fresh metadata.
		Busy,
	};

	// Admission for one new capture. Busy is temporary; unsupported or oversized
	// targets cannot become capturable by waiting for existing work to finish.
	enum class FrameCaptureAvailability : uint8_t
	{
		Available,
		Busy,
		Unsupported,
		TooLarge,
		Finalized,
	};

	// CPU copy of one captured display target: tightly packed, top-down rows
	// holding the bytes exactly as stored in the display-target format.
	struct FrameCaptureImage
	{
		RHIFormat m_Format = RHIFormat::Unknown;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		std::vector<uint8_t> m_Pixels;
	};

	struct FrameCaptureResult
	{
		uint64_t m_RequestId = 0;
		FrameCaptureSource m_Source = FrameCaptureSource::Scene;
		FrameCaptureStatus m_Status = FrameCaptureStatus::Failed;
		// Serial of the render host frame that recorded the capture; 0 when the
		// request finished before any frame recorded it.
		uint64_t m_FrameSerial = 0;
		// Actual recorded diagnostic tap; absent for Scene and Composited.
		std::optional<PostProcessDebugTap> m_DiagnosticTap;
		// Present only for Completed results. Requests captured by the same frame
		// and tap share one immutable image.
		std::shared_ptr<const FrameCaptureImage> m_Image;
		std::shared_ptr<const RGSnapshot> m_Graph;
		std::shared_ptr<const ResourceExportPayload> m_Resources;
		RGObservationFailure m_ResourceFailure = RGObservationFailure::None;
		bool m_GraphExportActive = false;
		bool m_ResourceExportActive = false;
		uint32_t m_SubmissionFenceIndex = 0;
		uint32_t m_SubmissionFenceGeneration = 0;
		uint64_t m_SubmissionFenceValue = 0;
		std::string m_AdapterCompatibilityIdentity;
		std::string m_Failure;
	};

	[[nodiscard]] std::string_view GetFrameCaptureSourceName(FrameCaptureSource source) noexcept;

	// Stable name of a tap that supports diagnostic captures, or empty for any other
	// tap. Diagnostic captures cover temporal, scene-depth and GTAO taps.
	[[nodiscard]] std::string_view GetFrameCaptureDiagnosticTapName(
		PostProcessDebugTap tap) noexcept;
	[[nodiscard]] std::optional<PostProcessDebugTap> FindFrameCaptureDiagnosticTap(
		std::string_view name) noexcept;

	// Converts a captured image to 8-bit RGBA rows with opaque alpha. Display
	// targets store display-encoded values for both Unorm and UnormSrgb formats,
	// so the color bytes are copied without transfer-function conversion. Returns
	// nullopt for formats without a defined 8-bit display conversion.
	[[nodiscard]] std::optional<std::vector<uint8_t>> ConvertFrameCaptureToRgba8(
		const FrameCaptureImage& image) noexcept;
}
