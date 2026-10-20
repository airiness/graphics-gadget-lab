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

	// The legacy adapter preserves Runtime rejection instead of treating an
	// unsupported input source as an image.
	struct UnsupportedCaptureProduct
	{
		FrameCaptureSource m_Source = FrameCaptureSource::Scene;
	};

	using CaptureProductSpec = std::variant<ImageCaptureProduct, DiagnosticCaptureProduct,
		GraphCaptureProduct, ResourceCaptureProduct, UnsupportedCaptureProduct>;

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
		return std::get<UnsupportedCaptureProduct>(product).m_Source;
	}

	// AppRuntime's internal SameFrame plan, not a versioned host/protocol request.
	// Every listed product is required. One resource product and one graph product
	// are supported; their legacy delivery shares one result and completion marker.
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
