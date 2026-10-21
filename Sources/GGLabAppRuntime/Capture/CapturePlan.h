#pragma once
#include "Capture/FrameCaptureMetadata.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace gglab
{
	enum class CaptureImageSource : uint8_t
	{
		Scene,
		Composited,
	};

	struct ImageCaptureProduct
	{
		CaptureImageSource m_Source = CaptureImageSource::Scene;
	};

	struct DiagnosticCaptureProduct
	{
		std::optional<PostProcessDebugTap> m_Tap;
	};

	struct GraphCaptureProduct
	{
		bool m_Summary = false;
	};

	struct ResourceCaptureProduct
	{
		std::vector<RGResourceAddress> m_Targets;
	};

	using CaptureProductSpec = std::variant<ImageCaptureProduct, DiagnosticCaptureProduct,
		GraphCaptureProduct, ResourceCaptureProduct>;

	[[nodiscard]] inline FrameCaptureSource GetCaptureProductSource(
		const CaptureProductSpec& product) noexcept
	{
		if (const auto* image = std::get_if<ImageCaptureProduct>(&product))
		{
			return image->m_Source == CaptureImageSource::Scene ?
				FrameCaptureSource::Scene :
				FrameCaptureSource::Composited;
		}
		if (std::holds_alternative<DiagnosticCaptureProduct>(product))
		{
			return FrameCaptureSource::Diagnostic;
		}
		if (std::holds_alternative<GraphCaptureProduct>(product))
		{
			return FrameCaptureSource::RenderGraph;
		}
		if (std::holds_alternative<ResourceCaptureProduct>(product))
		{
			return FrameCaptureSource::ResourceExport;
		}
		return FrameCaptureSource::ResourceExport;
	}

	// Host-neutral SameFrame job plan. Every listed product is required and is
	// represented in the final bundle. A resource and its graph share one producer
	// delivery while retaining separate ordered product results.
	struct CapturePlan
	{
		std::vector<CaptureProductSpec> m_RequiredProducts;
		FrameCaptureTiming m_Timing = FrameCaptureTiming::NextFrame;
		uint32_t m_SettleFrames = 0;
		std::string m_RequiredContentId;
		std::string m_ReferenceViewId;
		bool m_WaitForCapacity = false;
		std::filesystem::path m_OutputDirectory;
		std::string m_Label;
		std::string m_Note;
		// Provenance only: membership never depends on these values matching.
		std::optional<FrameCaptureSequenceInfo> m_Sequence;
	};
}
