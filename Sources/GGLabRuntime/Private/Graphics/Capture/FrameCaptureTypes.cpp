#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace gglab
{
	std::string_view GetFrameCaptureSourceName(FrameCaptureSource source) noexcept
	{
		switch (source)
		{
		case FrameCaptureSource::Scene:
			return "scene";
		case FrameCaptureSource::Composited:
			return "composited";
		case FrameCaptureSource::Diagnostic:
			return "diagnostic";
		}
		GGLAB_UNREACHABLE("Unhandled FrameCaptureSource.");
	}

	namespace
	{
		// Persisted in capture metadata and requested by name; never rename an entry.
		constexpr std::array<std::pair<PostProcessDebugTap, std::string_view>, 19>
			DiagnosticTapNames{ {
				{ PostProcessDebugTap::SceneDepthRaw, "scene-depth-raw" },
				{ PostProcessDebugTap::SceneDepthLinearViewZ, "scene-depth-linear-view-z" },
				{ PostProcessDebugTap::GTAORawAO, "gtao-raw-ao" },
				{ PostProcessDebugTap::GTAOHalfDepthViewZ, "gtao-half-depth-view-z" },
				{ PostProcessDebugTap::GTAOReconstructedNormal, "gtao-reconstructed-normal" },
				{ PostProcessDebugTap::GTAOSelectedSurfaceOffset, "gtao-selected-surface-offset" },
				{ PostProcessDebugTap::GTAODenoiseX, "gtao-denoise-x" },
				{ PostProcessDebugTap::GTAODenoiseY, "gtao-denoise-y" },
				{ PostProcessDebugTap::GTAOFinalAO, "gtao-final-ao" },
				{ PostProcessDebugTap::GTAOAOOnlyLightingContribution, "gtao-ao-only-lighting" },
				{ PostProcessDebugTap::TemporalMotionDirection, "temporal-motion-direction" },
				{ PostProcessDebugTap::TemporalMotionMagnitude, "temporal-motion-magnitude" },
				{ PostProcessDebugTap::TemporalHistoryColor, "temporal-history-color" },
				{ PostProcessDebugTap::TemporalReprojectionUV, "temporal-reprojection-uv" },
				{ PostProcessDebugTap::TemporalRejection, "temporal-rejection" },
				{ PostProcessDebugTap::TemporalHistoryWeight, "temporal-history-weight" },
				{ PostProcessDebugTap::TemporalHistorySamples, "temporal-history-samples" },
				{ PostProcessDebugTap::TemporalClipDistance, "temporal-clip-distance" },
				{ PostProcessDebugTap::TemporalHistoryRelaxation, "temporal-history-relaxation" },
			} };
	}

	std::string_view GetFrameCaptureDiagnosticTapName(PostProcessDebugTap tap) noexcept
	{
		for (const auto& [candidate, name] : DiagnosticTapNames)
		{
			if (candidate == tap)
			{
				return name;
			}
		}
		return {};
	}

	std::optional<PostProcessDebugTap> FindFrameCaptureDiagnosticTap(
		std::string_view name) noexcept
	{
		for (const auto& [tap, candidate] : DiagnosticTapNames)
		{
			if (candidate == name)
			{
				return tap;
			}
		}
		return std::nullopt;
	}

	std::optional<std::vector<uint8_t>> ConvertFrameCaptureToRgba8(
		const FrameCaptureImage& image) noexcept
	{
		bool swapRedBlue = false;
		switch (image.m_Format)
		{
		case RHIFormat::R8G8B8A8Unorm:
		case RHIFormat::R8G8B8A8UnormSrgb:
			break;
		case RHIFormat::B8G8R8A8Unorm:
		case RHIFormat::B8G8R8A8UnormSrgb:
			swapRedBlue = true;
			break;
		default:
			return std::nullopt;
		}

		const size_t pixelCount = static_cast<size_t>(image.m_Width) * image.m_Height;
		if (pixelCount == 0 || image.m_Pixels.size() != pixelCount * 4)
		{
			return std::nullopt;
		}

		std::vector<uint8_t> rgba(image.m_Pixels.size());
		for (size_t pixel = 0; pixel < pixelCount; ++pixel)
		{
			const uint8_t* source = image.m_Pixels.data() + pixel * 4;
			uint8_t* destination = rgba.data() + pixel * 4;
			destination[0] = swapRedBlue ? source[2] : source[0];
			destination[1] = source[1];
			destination[2] = swapRedBlue ? source[0] : source[2];
			// Display-target alpha is not composited by presentation.
			destination[3] = 0xFF;
		}
		return rgba;
	}
}
