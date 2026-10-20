#pragma once
#include "Capture/CapturePublisher.h"
#include "Capture/FrameCaptureImageEncoder.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/ResourceExportWriter.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace gglab
{
	struct CaptureProductEncodeInput
	{
		std::shared_ptr<const FrameCaptureImage> m_Image;
		std::shared_ptr<const RGSnapshot> m_Graph;
		std::shared_ptr<const ResourceExportPayload> m_Resources;
		bool m_ResourceExportActive = false;
		bool m_GraphSummary = false;
		ResourceExportSubmission m_Submission;
		FrameCaptureMetadata m_Metadata;
	};

	struct CaptureEncodedProduct
	{
		CapturePublicationContent m_Content;
		std::optional<size_t> m_ImageIndex;
		std::optional<size_t> m_GraphIndex;
		size_t m_ResourceCount = 0;
		std::string m_Failure;
	};

	// Encoders own format interpretation, never file I/O or publication. Sidecar
	// serializers receive resolved names from the publisher for every candidate.
	[[nodiscard]] CaptureEncodedProduct EncodeCaptureProduct(const CaptureProductEncodeInput& input,
		const FrameCaptureImageEncoder& imageEncoder, std::atomic<CaptureWriteStage>& stage) noexcept;
}
