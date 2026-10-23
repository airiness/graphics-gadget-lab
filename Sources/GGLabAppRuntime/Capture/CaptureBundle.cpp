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
			"camera", "time", "developmentTools", "readiness", "temporal", "sequence", "parameters" };

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

	std::string MakeSequenceArtifactBundleId(
		std::string_view runIdentity, uint64_t sequenceId, bool parameters)
	{
		return parameters ? std::format("{}-s{}-range", runIdentity, sequenceId) :
			MakeSequenceTimingBundleId(runIdentity, sequenceId);
	}

	CaptureEncodedBundle EncodeSequenceArtifactBundle(const SequenceArtifactBundleInput& input) noexcept try
	{
		const Json environment = Json::parse(input.m_Environment);
		if (input.m_RunIdentity.size() != 32 || !std::ranges::all_of(input.m_RunIdentity,
			[](char value) { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); }) ||
			input.m_SequenceId == 0 || !environment.is_object() ||
			(input.m_TimingJson.empty() && input.m_ParameterFramesJson.empty()))
		{
			return { .m_Failure = "A sequence bundle requires environment, sequence identity and products." };
		}
		CaptureEncodedBundle bundle;
		bundle.m_BundleId = MakeSequenceArtifactBundleId(input.m_RunIdentity,
			input.m_SequenceId, !input.m_ParameterFramesJson.empty());
		Json sequence;
		Json watch;
		Json descriptions = Json::array();
		std::vector<uint64_t> timingSerials;
		const auto append = [&](const std::string& bytes, bool parameters)
			{
				if (bytes.empty())
				{
					return;
				}
				const Json product = Json::parse(bytes);
				const auto& range = product.at("sequence");
				const uint64_t requested = uint64_t(range.at("frameCount")) *
					std::max(uint64_t{ 1 }, range.at("referenceSamples").get<uint64_t>());
				const auto& samples = parameters ? product.at("frames") : product.at("coverage").at("samples");
				if (product.at("schema") != (parameters ? "gglab.parameter-frames" : "gglab.gpu-timing") ||
					product.at("schemaVersion") != 1 || range.at("id") != input.m_SequenceId ||
					requested == 0 || samples.size() != requested ||
					(!parameters && (product.at("coverage").at("requestedSamples") != requested ||
						product.at("coverage").at("recordedSamples") != requested ||
						product.at("frame").at("milliseconds").size() != requested)))
				{
					throw std::runtime_error("A sequence product requires complete submitted sample coverage.");
				}
				uint64_t previous = 0;
				const uint64_t perFrame = std::max(uint64_t{ 1 }, range.at("referenceSamples").get<uint64_t>());
				if (!parameters)
				{
					timingSerials.resize(samples.size());
					for (const auto& sample : samples)
					{
						const uint64_t frame = sample.at("sequenceFrame");
						const uint64_t reference = sample.at("referenceSample");
						const uint64_t index = frame * perFrame + reference;
						if (frame >= range.at("frameCount").get<uint64_t>() || reference >= perFrame ||
							index >= timingSerials.size() || timingSerials[index] != 0)
						{
							throw std::runtime_error("Timing samples must cover distinct sequence samples.");
						}
						timingSerials[index] = sample.at("frameSerial");
					}
					for (const auto serial : timingSerials)
					{
						if (serial <= previous)
						{
							throw std::runtime_error("Timing sample serials must follow sequence submission order.");
						}
						previous = serial;
					}
					if (timingSerials.front() != range.at("firstFrameSerial") ||
						timingSerials.back() != range.at("lastFrameSerial"))
					{
						throw std::runtime_error("Timing samples disagree with the sequence range.");
					}
					previous = 0;
				}
				for (size_t index = 0; parameters && index < samples.size(); ++index)
				{
					const auto& sample = samples[index];
					const uint64_t serial = sample.at("parameters").at("frameSerial");
					if (serial <= previous || sample.at("sequenceFrame") != index / perFrame ||
						sample.at("referenceSample") != index % perFrame ||
						(index == 0 && serial != range.at("firstFrameSerial")) ||
						(index + 1 == samples.size() && serial != range.at("lastFrameSerial")) ||
						(!timingSerials.empty() && (index >= timingSerials.size() || serial != timingSerials[index])))
					{
						throw std::runtime_error("Sequence products disagree on the submitted frame grid.");
					}
					previous = serial;
				}
				if (sequence.is_null())
				{
					sequence = range;
					watch = product.at("watch");
				}
				else if (sequence != range || watch != product.at("watch"))
				{
					throw std::runtime_error("Sequence products disagree on range or watch coverage.");
				}
				const auto index = static_cast<uint32_t>(descriptions.size());
				descriptions.push_back({
					{ "index", index },
					{ "source", parameters ? "parameter-frames" : "gpu-timing" },
					{ "diagnosticTap", nullptr },
					{ "schema", product.at("schema") },
					{ "schemaVersion", 1 } });
				bundle.m_Content.m_Files.push_back({
					.m_Suffix = parameters ? ".parameter-frames.json" : ".gpu-timing.json",
					.m_Bytes = std::vector<uint8_t>(bytes.begin(), bytes.end()),
					.m_Stage = CaptureWriteStage::WritingMetadata });
				bundle.m_Products.push_back({ index, { index } });
			};
		append(input.m_TimingJson, false);
		append(input.m_ParameterFramesJson, true);
		bundle.m_Content.m_Files.push_back({ .m_Suffix = ".capture-bundle.json",
			.m_Stage = CaptureWriteStage::WritingMetadata });
		bundle.m_Content.m_MakeMarker = [identity = bundle.m_BundleId, run = input.m_RunIdentity,
			environment, sequence, watch, descriptions](std::span<const std::filesystem::path> paths)
			{
				Json products = descriptions;
				for (size_t index = 0; index < products.size(); ++index)
				{
					const auto file = GetFrameCapturePathText(paths[index].filename());
					products[index]["metadata"] = file;
					products[index]["files"] = Json::array({ file });
				}
				return Json{
					{ "schema", "gglab.capture-bundle" },
					{ "schemaVersion", CaptureBundleSchemaVersion },
					{ "bundleId", identity },
					{ "runIdentity", run },
					{ "consistency", "sequence-range" },
					{ "environment", environment },
					{ "sequence", sequence },
					{ "watch", watch },
					{ "products", std::move(products) } }.dump(2);
			};
		return bundle;
	}
	catch (const std::exception& error)
	{
		return { .m_Failure = std::format("Encoding the sequence bundle failed: {}", error.what()) };
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
