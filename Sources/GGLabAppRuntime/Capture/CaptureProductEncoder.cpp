#include "Capture/CaptureProductEncoder.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"

#include <exception>
#include <format>
#include <utility>

namespace gglab
{
	namespace
	{
		[[nodiscard]] std::vector<uint8_t> JsonBytes(const std::string& json)
		{
			return { json.begin(), json.end() };
		}
	}

	CaptureEncodedProduct EncodeCaptureProduct(const CaptureProductEncodeInput& input,
		const FrameCaptureImageEncoder& imageEncoder, std::atomic<CaptureWriteStage>& stage) noexcept try
	{
		CaptureEncodedProduct product;
		auto& content = product.m_Content;
		if (input.m_Resources)
		{
			stage = CaptureWriteStage::SerializingGraph;
			product.m_ResourceCount = input.m_Resources->m_Arrays.size();
			for (size_t i = 0; i < product.m_ResourceCount; ++i)
			{
				const auto& array = input.m_Resources->m_Arrays[i];
				content.m_Files.push_back({ .m_Suffix = std::format(".{}.npy", i),
					.m_Bytes = MakeResourceExportNpyHeader(array),
					.m_Tail = { input.m_Resources, &array.m_Data } });
			}
			if (input.m_Graph)
			{
				product.m_GraphIndex = content.m_Files.size();
				content.m_Files.push_back({ .m_Suffix = ".graph.json",
					.m_Bytes = JsonBytes(SerializeFrameGraphExport(*input.m_Graph,
						input.m_Metadata, input.m_Submission, false, true)) });
			}
			content.m_Files.push_back({ .m_Suffix = ".json",
				.m_Stage = CaptureWriteStage::WritingMetadata });
			content.m_MakeMarker = [resources = input.m_Resources, metadata = input.m_Metadata,
				submission = input.m_Submission, count = product.m_ResourceCount,
				graph = product.m_GraphIndex](std::span<const std::filesystem::path> paths)
				{
					return SerializeResourceExportMetadata(*resources, metadata, submission,
						paths.first(count), graph ? paths[*graph] : std::filesystem::path{});
				};
			return product;
		}
		if (input.m_Graph)
		{
			stage = CaptureWriteStage::SerializingGraph;
			content.m_Files.push_back({ .m_Suffix = ".json",
				.m_Bytes = JsonBytes(SerializeFrameGraphExport(*input.m_Graph, input.m_Metadata,
					input.m_Submission, input.m_GraphSummary, input.m_ResourceExportActive)) });
			return product;
		}
		if (!imageEncoder)
		{
			return { .m_Failure = "No capture image encoder is configured." };
		}
		stage = CaptureWriteStage::Encoding;
		auto bytes = input.m_Image ? imageEncoder(*input.m_Image) : std::nullopt;
		if (!bytes)
		{
			return { .m_Failure = "The captured image could not be encoded as PNG." };
		}
		product.m_ImageIndex = 0;
		content.m_Files.push_back({ .m_Suffix = ".png", .m_Bytes = std::move(*bytes),
			.m_Stage = CaptureWriteStage::WritingImage });
		content.m_Files.push_back({ .m_Suffix = ".json",
			.m_Stage = CaptureWriteStage::WritingMetadata });
		content.m_MakeMarker = [metadata = input.m_Metadata]
			(std::span<const std::filesystem::path> paths) mutable
			{
				metadata.m_ImageFile = GetFrameCapturePathText(paths.front().filename());
				return SerializeFrameCaptureMetadata(metadata);
			};
		return product;
	}
	catch (const std::exception& error)
	{
		return { .m_Failure = std::format("Serializing the {} export failed: {}",
			input.m_Resources ? "resource" : "graph", error.what()) };
	}
}
