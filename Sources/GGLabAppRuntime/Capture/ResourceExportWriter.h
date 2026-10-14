#pragma once
#include "Capture/FrameCaptureMetadata.h"
#include "GGLabRuntime/Graphics/Capture/ResourceExportTypes.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	struct RGSnapshot;
	struct ResourceExportSubmission
	{
		uint32_t m_FenceIndex = 0;
		uint32_t m_FenceGeneration = 0;
		uint64_t m_FenceValue = 0;
		std::string m_AdapterIdentity;
	};
	[[nodiscard]] std::vector<uint8_t> MakeResourceExportNpyHeader(const ResourceExportArray& array);
	[[nodiscard]] std::string SerializeResourceExportMetadata(const ResourceExportPayload& payload,
		const FrameCaptureMetadata& metadata, const ResourceExportSubmission& submission,
		std::span<const std::filesystem::path> files, const std::filesystem::path& graphFile);
	[[nodiscard]] std::string SerializeFrameGraphExport(const RGSnapshot& snapshot,
		const FrameCaptureMetadata& metadata, const ResourceExportSubmission& submission,
		bool summary, bool resourceExportActive);
}
