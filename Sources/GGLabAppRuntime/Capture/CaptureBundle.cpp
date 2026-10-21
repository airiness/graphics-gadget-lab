#include "Capture/CaptureBundle.h"
#include "Capture/NonFiniteWatchCoordinator.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <exception>
#include <format>
#include <functional>
#include <random>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace gglab
{
	namespace
	{
		using Json = nlohmann::ordered_json;
		constexpr std::array ProvenanceKeys{ "backend", "content", "sceneInputs", "frame",
			"camera", "time", "developmentTools", "readiness", "temporal", "sequence" };

		Json Provenance(const FrameCaptureMetadata& metadata)
		{
			const auto frame = Json::parse(SerializeFrameCaptureMetadata(metadata));
			Json provenance = Json::object();
			for (const auto key : ProvenanceKeys)
			{
				provenance[key] = frame.at(key);
			}
			return provenance;
		}

		Json Submission(const ResourceExportSubmission& submission)
		{
			return Json{
				{ "fenceIndex", submission.m_FenceIndex },
				{ "fenceGeneration", submission.m_FenceGeneration },
				{ "fenceValue", submission.m_FenceValue },
				{ "adapterDriverIdentity", submission.m_AdapterIdentity } };
		}

		struct ProductRecord
		{
			Json m_Description;
			std::vector<size_t> m_Files;
			size_t m_MetadataIndex = 0;
		};
	}

	std::string CreateCaptureRunIdentity() noexcept try
	{
		std::random_device random;
		std::string identity;
		for (uint32_t word = 0; word < 4; ++word)
		{
			identity += std::format("{:08x}", static_cast<uint32_t>(random()));
		}
		return identity;
	}
	catch (...)
	{
		return {};
	}

	std::string MakeCaptureBundleId(std::string_view runIdentity, uint64_t jobId, uint64_t attempt)
	{
		return std::format("{}-j{}-a{}", runIdentity, jobId, attempt);
	}

	std::string MakeSequenceTimingBundleId(std::string_view runIdentity, uint64_t sequenceId)
	{
		return std::format("{}-s{}-timing", runIdentity, sequenceId);
	}

	CaptureEncodedBundle EncodeSequenceTimingBundle(const SequenceTimingBundleInput& input) noexcept try
	{
		const Json environment = Json::parse(input.m_Environment);
		const Json product = Json::parse(input.m_ProductJson);
		const auto& sequence = product.at("sequence");
		const uint64_t requested = product.at("coverage").at("requestedSamples");
		if (input.m_RunIdentity.size() != 32 || !std::ranges::all_of(input.m_RunIdentity,
			[](char value) { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); }) ||
			input.m_SequenceId == 0 || !environment.is_object() ||
			product.at("schema") != "gglab.gpu-timing" || product.at("schemaVersion") != 1 ||
			product.at("sequence").at("id") != input.m_SequenceId ||
			requested == 0 || product.at("coverage").at("recordedSamples") != requested ||
			product.at("coverage").at("samples").size() != requested ||
			product.at("frame").at("milliseconds").size() != requested ||
			requested != sequence.at("frameCount").get<uint64_t>() *
				std::max(uint64_t{ 1 }, sequence.at("referenceSamples").get<uint64_t>()))
		{
			return { .m_Failure = "A timing bundle requires environment, sequence identity and complete samples." };
		}
		CaptureEncodedBundle bundle;
		bundle.m_BundleId = MakeSequenceTimingBundleId(input.m_RunIdentity, input.m_SequenceId);
		bundle.m_Content.m_Files.push_back({ .m_Suffix = ".gpu-timing.json",
			.m_Bytes = std::vector<uint8_t>(input.m_ProductJson.begin(), input.m_ProductJson.end()),
			.m_Stage = CaptureWriteStage::WritingMetadata });
		bundle.m_Products.push_back({ 0, { 0 } });
		bundle.m_Content.m_Files.push_back({ .m_Suffix = ".capture-bundle.json",
			.m_Stage = CaptureWriteStage::WritingMetadata });
		bundle.m_Content.m_MakeMarker = [identity = bundle.m_BundleId, run = input.m_RunIdentity,
			environment, sequence = product.at("sequence"), watch = product.at("watch")]
			(std::span<const std::filesystem::path> paths)
			{
				const std::string file = GetFrameCapturePathText(paths[0].filename());
				return Json{
					{ "schema", "gglab.capture-bundle" },
					{ "schemaVersion", CaptureBundleSchemaVersion },
					{ "bundleId", identity },
					{ "runIdentity", run },
					{ "consistency", "sequence-range" },
					{ "environment", environment },
					{ "sequence", sequence },
					{ "watch", watch },
					{ "products", Json::array({ Json{
						{ "index", 0 },
						{ "source", "gpu-timing" },
						{ "diagnosticTap", nullptr },
						{ "schema", "gglab.gpu-timing" },
						{ "schemaVersion", 1 },
						{ "metadata", file },
						{ "files", Json::array({ file }) } } }) } }.dump(2);
			};
		return bundle;
	}
	catch (const std::exception& error)
	{
		return { .m_Failure = std::format("Encoding the timing bundle failed: {}", error.what()) };
	}

	CaptureEncodedBundle EncodeCaptureBundle(const CaptureBundleInput& input,
		const FrameCaptureImageEncoder& imageEncoder, std::atomic<CaptureWriteStage>& stage) noexcept try
	{
		if (input.m_RunIdentity.size() != 32 || !std::ranges::all_of(input.m_RunIdentity, [](char value)
			{ return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); }) ||
			input.m_JobId == 0 || input.m_Attempt == 0 || input.m_ProductCount == 0 || input.m_Deliveries.empty())
		{
			return { .m_Failure = "A bundle requires qualified job/attempt identity and required products." };
		}
		const Json environment = Json::parse(input.m_Environment, nullptr, false);
		if (!environment.is_object())
		{
			return { .m_Failure = "A bundle requires the process environment record." };
		}
		CaptureEncodedBundle bundle;
		bundle.m_BundleId = MakeCaptureBundleId(input.m_RunIdentity, input.m_JobId, input.m_Attempt);
		const auto& first = input.m_Deliveries.front().m_Input;
		const auto provenance = Provenance(first.m_Metadata);
		const auto submission = Submission(first.m_Submission);
		const auto watch = Json::parse(SerializeFrameCaptureMetadata(first.m_Metadata)).value("watch", Json(nullptr));
		const auto& coverage = first.m_Metadata.m_WatchCoverage;
		if (coverage && (!coverage->m_Complete || coverage->Fails() || coverage->m_FrameCount != 1 ||
			coverage->m_CompletedFrames != 1 || coverage->m_FirstFrameSerial != first.m_Metadata.m_FrameSerial ||
			coverage->m_LastFrameSerial != first.m_Metadata.m_FrameSerial))
		{
			return { .m_Failure = "A bundle requires resolved watch coverage for its submitted frame." };
		}
		std::vector<std::optional<ProductRecord>> records(input.m_ProductCount);
		for (const auto& delivery : input.m_Deliveries)
		{
			const auto& value = delivery.m_Input;
			const auto source = value.m_Metadata.m_Source;
			const bool resource = source == FrameCaptureSource::ResourceExport;
			const bool graph = source == FrameCaptureSource::RenderGraph;
			const bool image = source == FrameCaptureSource::Scene || source == FrameCaptureSource::Composited ||
				source == FrameCaptureSource::Diagnostic;
			if ((!resource && !graph && !image) || bool(value.m_Resources) != resource ||
				bool(value.m_Image) != image || bool(value.m_Graph) != (graph || delivery.m_GraphProductIndex.has_value()) ||
				(delivery.m_GraphProductIndex && !resource) || value.m_Metadata.m_FrameSerial == 0 ||
				value.m_Submission.m_FenceValue == 0)
			{
				return { .m_Failure = "Bundle payloads must match their required product and completed frame submission." };
			}
			if (Provenance(value.m_Metadata) != provenance || Submission(value.m_Submission) != submission ||
				Json::parse(SerializeFrameCaptureMetadata(value.m_Metadata)).value("watch", Json(nullptr)) != watch)
			{
				return { .m_Failure = "Required bundle products disagree on frame provenance, submission or watch coverage." };
			}
			auto encoded = EncodeCaptureProduct(value, imageEncoder, stage);
			if (!encoded.m_Failure.empty())
			{
				return { .m_Failure = std::move(encoded.m_Failure) };
			}
			const size_t offset = bundle.m_Content.m_Files.size();
			const size_t count = encoded.m_Content.m_Files.size();
			const std::string prefix = std::format(".p{}{}", delivery.m_ProductIndex,
				value.m_Resources ? ".resource-export" :
					value.m_Graph ? ".graph" : "");
			for (size_t index = 0; index < count; ++index)
			{
				auto file = std::move(encoded.m_Content.m_Files[index]);
				file.m_Suffix = prefix + file.m_Suffix;
				if (index + 1 == count && encoded.m_Content.m_MakeMarker)
				{
					file.m_MakeBytes = [make = std::move(encoded.m_Content.m_MakeMarker), offset, count]
						(std::span<const std::filesystem::path> paths)
						{
							return make(paths.subspan(offset, count));
						};
				}
				bundle.m_Content.m_Files.push_back(std::move(file));
			}
			const auto record = [&](uint32_t index, bool graphProduct)
				{
					if (index >= records.size() || records[index])
					{
						throw std::runtime_error("Bundle product indices must be distinct and complete.");
					}
					ProductRecord product;
					const auto productSource = graphProduct ? FrameCaptureSource::RenderGraph : value.m_Metadata.m_Source;
					product.m_Description = Json{
						{ "index", index },
						{ "source", GetFrameCaptureSourceName(productSource) },
						{ "diagnosticTap", value.m_Metadata.m_DiagnosticTap.empty() ?
							Json(nullptr) : Json(value.m_Metadata.m_DiagnosticTap) },
						{ "schema", graphProduct ? "gglab.render-graph" :
							value.m_Resources ? "gglab.resource-export" : "gglab.frame-capture" } };
					if (graphProduct)
					{
						const size_t graphIndex = value.m_Resources ? *encoded.m_GraphIndex : 0;
						product.m_MetadataIndex = offset + graphIndex;
						product.m_Files.push_back(product.m_MetadataIndex);
					}
					else
					{
						product.m_MetadataIndex = offset + count - 1;
						for (size_t file = 0; file < count; ++file)
						{
							if (!encoded.m_GraphIndex || file != *encoded.m_GraphIndex)
							{
								product.m_Files.push_back(offset + file);
							}
						}
					}
					// Versions are read from the actual owning serializer's output below.
					records[index] = std::move(product);
				};
			record(delivery.m_ProductIndex, bool(value.m_Graph) && !value.m_Resources);
			if (delivery.m_GraphProductIndex)
			{
				if (!value.m_Resources || !encoded.m_GraphIndex)
				{
					return { .m_Failure = "A paired graph product requires its resource delivery's graph." };
				}
				record(*delivery.m_GraphProductIndex, true);
			}
		}
		if (std::ranges::any_of(records, [](const auto& record) { return !record; }))
		{
			return { .m_Failure = "Every required bundle product must have one delivery." };
		}
		// Freeze references to serializer bytes/callbacks without duplicating image or
		// array payloads. The writer owns these callbacks until publication finishes.
		std::vector<std::function<uint32_t(std::span<const std::filesystem::path>)>> versions;
		for (const auto& record : records)
		{
			const auto& file = bundle.m_Content.m_Files[record->m_MetadataIndex];
			if (file.m_MakeBytes)
			{
				versions.push_back([make = file.m_MakeBytes](auto paths)
					{
						return Json::parse(make(paths)).at("schemaVersion").template get<uint32_t>();
					});
			}
			else
			{
				const auto version = Json::parse(file.m_Bytes).at("schemaVersion").get<uint32_t>();
				versions.push_back([version](auto)
					{
						return version;
					});
			}
			bundle.m_Products.push_back({ static_cast<uint32_t>(bundle.m_Products.size()), record->m_Files });
		}
		bundle.m_Content.m_Files.push_back({ .m_Suffix = ".capture-bundle.json",
			.m_Stage = CaptureWriteStage::WritingMetadata });
		bundle.m_Content.m_MakeMarker = [records = std::move(records), versions = std::move(versions),
			environment, provenance, submission, watch, run = input.m_RunIdentity, id = input.m_JobId,
			attempt = input.m_Attempt, identity = bundle.m_BundleId](std::span<const std::filesystem::path> paths)
			{
				Json products = Json::array();
				for (size_t index = 0; index < records.size(); ++index)
				{
					const auto& record = *records[index];
					auto product = record.m_Description;
					product["schemaVersion"] = versions[index](paths);
					product["metadata"] = GetFrameCapturePathText(paths[record.m_MetadataIndex].filename());
					product["files"] = Json::array();
					for (const auto file : record.m_Files)
					{
						product["files"].push_back(GetFrameCapturePathText(paths[file].filename()));
					}
					products.push_back(std::move(product));
				}
				return Json{
					{ "schema", "gglab.capture-bundle" },
					{ "schemaVersion", CaptureBundleSchemaVersion },
					{ "bundleId", identity },
					{ "runIdentity", run },
					{ "jobId", id },
					{ "attempt", attempt },
					{ "consistency", "same-frame" },
					{ "environment", environment },
					{ "provenance", provenance },
					{ "submission", submission },
					{ "watch", watch },
					{ "products", std::move(products) } }.dump(2);
			};
		return bundle;
	}
	catch (const std::exception& error)
	{
		return { .m_Failure = std::format("Encoding the capture bundle failed: {}", error.what()) };
	}
}
