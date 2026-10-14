#include "Capture/ResourceExportWriter.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Diagnostics/RenderGraphEnumText.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"

#include <format>
#include <limits>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace gglab
{
	namespace
	{
		using Json = nlohmann::ordered_json;
		Json Frame(const FrameCaptureMetadata& metadata, const ResourceExportSubmission& submission)
		{
			Json frame = Json::parse(SerializeFrameCaptureMetadata(metadata));
			frame.erase("image");
			frame.erase("schemaVersion");
			frame["adapterDriverIdentity"] = submission.m_AdapterIdentity;
			frame["codeIdentity"] = { {"status", "unavailable"}, {"reason", "The capture metadata service has no build identity source."} };
			frame["shaderIdentity"] = { {"status", "unavailable"}, {"reason", "The graph does not retain per-pass shader artifact identities."} };
			frame["parameterRevision"] = nullptr;
			return frame;
		}
		Json Submission(const ResourceExportSubmission& submission)
		{
			return { {"fenceIndex", submission.m_FenceIndex}, {"fenceGeneration", submission.m_FenceGeneration},
				{"fenceValue", submission.m_FenceValue}, {"completed", true} };
		}
		Json Address(const RGResourceAddress& address)
		{
			Json at = address.m_Position == RGObservationPosition::Final ? Json("final")
				: Json{{address.m_Position == RGObservationPosition::Before ? "before" : "after", address.m_Pass}};
			return { {"resource", address.m_Resource}, {"at", at}, {"occurrence", address.m_Occurrence ? Json(*address.m_Occurrence) : Json(nullptr)},
				{"mip", address.m_Mip}, {"slices", {address.m_FirstSlice, address.m_SliceCount.value_or(1)}},
				{"aspect", diagnostics::RGFlagsId(address.m_Aspect)} };
		}
		uint32_t ScalarBytes(ResourceExportScalar scalar)
		{
			return scalar == ResourceExportScalar::Uint8 ? 1 : scalar == ResourceExportScalar::Float16 ? 2 : 4;
		}
		std::string PythonString(std::string_view text)
		{
			std::string result = "'";
			for (const unsigned char value : text)
			{
				if (value == '\'' || value == '\\')
				{
					result += '\\';
				}
				if (value < 32 || value > 126)
				{
					result += std::format("\\x{:02x}", value);
				}
				else
				{
					result += static_cast<char>(value);
				}
			}
			return result + "'";
		}
	}

	std::vector<uint8_t> MakeResourceExportNpyHeader(const ResourceExportArray& array)
	{
		std::string dtype = PythonString(GetResourceExportDtype(array.m_Scalar));
		if (!array.m_Fields.empty())
		{
			dtype = "[";
			uint32_t offset = 0;
			for (const auto& field : array.m_Fields)
			{
				const uint64_t bytes = uint64_t{ ScalarBytes(field.m_Scalar) } * field.m_Count;
				if (field.m_Count == 0 || field.m_Offset < offset || field.m_Offset > array.m_Stride ||
					bytes > array.m_Stride - field.m_Offset)
				{
					throw std::runtime_error("Invalid registered GPU buffer layout.");
				}
				if (field.m_Offset != offset)
				{
					dtype += std::format("('', '|V{}'), ", field.m_Offset - offset);
				}
				dtype += std::format("({}, {}{}), ", PythonString(field.m_Name), PythonString(GetResourceExportDtype(field.m_Scalar)),
					field.m_Count == 1 ? std::string{} : std::format(", ({},)", field.m_Count));
				offset = field.m_Offset + static_cast<uint32_t>(bytes);
			}
			if (offset != array.m_Stride)
			{
				dtype += std::format("('', '|V{}'), ", array.m_Stride - offset);
			}
			dtype += "]";
		}
		uint64_t elements = 1;
		std::string shape = "(";
		for (const auto extent : array.m_Shape)
		{
			if (extent == 0 || elements > std::numeric_limits<uint64_t>::max() / extent)
			{
				throw std::runtime_error("Invalid resource array shape.");
			}
			elements *= extent;
			shape += std::format("{}, ", extent);
		}
		const uint32_t bytes = array.m_Fields.empty() ? ScalarBytes(array.m_Scalar) : array.m_Stride;
		if (array.m_Shape.empty() || bytes == 0 || elements > std::numeric_limits<uint64_t>::max() / bytes || elements * bytes != array.m_Data.size())
		{
			throw std::runtime_error("Resource array shape, dtype and byte count disagree.");
		}
		shape += ")";
		std::string header = std::format("{{'descr': {}, 'fortran_order': False, 'shape': {}, }}", dtype, shape);
		const size_t padding = (16 - ((10 + header.size() + 1) % 16)) % 16;
		header.append(padding, ' ');
		header += '\n';
		if (header.size() > UINT16_MAX)
		{
			throw std::runtime_error("Resource array header exceeds NumPy v1 limits.");
		}
		std::vector<uint8_t> result{ 0x93, 'N', 'U', 'M', 'P', 'Y', 1, 0,
			static_cast<uint8_t>(header.size()), static_cast<uint8_t>(header.size() >> 8) };
		result.insert(result.end(), header.begin(), header.end());
		return result;
	}

	std::string SerializeResourceExportMetadata(const ResourceExportPayload& payload,
		const FrameCaptureMetadata& metadata, const ResourceExportSubmission& submission,
		std::span<const std::filesystem::path> files, const std::filesystem::path& graphFile)
	{
		if (files.size() != payload.m_Arrays.size())
		{
			throw std::runtime_error("Resource export file count disagrees with its group.");
		}
		Json root = { {"schema", "gglab.resource-export"}, {"schemaVersion", 1}, {"frameSerial", metadata.m_FrameSerial},
			{"frame", Frame(metadata, submission)}, {"submission", Submission(submission)},
			{"observation", {{"graph", metadata.m_GraphExportActive}, {"resourceExport", true}, {"nonFiniteWatch", false}}},
			{"graph", graphFile.empty() ? Json(nullptr) : Json(GetFrameCapturePathText(graphFile.filename()))}, {"targets", Json::array()} };
		for (size_t i = 0; i < payload.m_Arrays.size(); ++i)
		{
			const auto& array = payload.m_Arrays[i];
			Json target = { {"address", Address(array.m_Address)}, {"resourceNodeIndex", array.m_ResourceNodeIndex},
				{"version", array.m_Version}, {"writerPassIndex", array.m_WriterPassIndex}, {"writerPass", array.m_WriterPass},
				{"writerOccurrence", array.m_WriterOccurrence}, {"type", diagnostics::RGEnumId(array.m_Type)},
				{"array", GetFrameCapturePathText(files[i].filename())}, {"physicalFormat", GetRHIFormatInfo(array.m_PhysicalFormat).m_Name},
				{"viewFormat", GetRHIFormatInfo(array.m_ViewFormat).m_Name}, {"rawTypeless", array.m_RawTypeless},
				{"dtype", array.m_Fields.empty() ? Json(GetResourceExportDtype(array.m_Scalar)) : Json("structured")},
				{"shape", array.m_Shape}, {"byteCount", array.m_Data.size()}, {"encoding", array.m_Encoding},
				{"channelOrder", array.m_ChannelOrder}, {"strideBytes", array.m_Stride}, {"elementCount", array.m_ElementCount},
				{"fields", Json::array()}, {"semantics", nullptr}, {"extendedLifetimes", Json::array()} };
			for (const auto& field : array.m_Fields) target["fields"].push_back({ {"name", field.m_Name}, {"offset", field.m_Offset},
				{"dtype", GetResourceExportDtype(field.m_Scalar)}, {"count", field.m_Count} });
			if (array.m_Semantics)
			{
				const auto& semantics = *array.m_Semantics;
				target["semantics"] = { {"role", semantics.m_Role}, {"units", semantics.m_Units}, {"space", semantics.m_Space},
					{"description", semantics.m_Description}, {"preExposure", semantics.m_PreExposure ? Json(*semantics.m_PreExposure) : Json(nullptr)},
					{"reversedDepth", semantics.m_ReversedDepth ? Json(*semantics.m_ReversedDepth) : Json(nullptr)},
					{"watchTier", semantics.m_WatchTier ? Json(*semantics.m_WatchTier) : Json(nullptr)} };
			}
			if (array.m_ObservedLastUse != array.m_OriginalLastUse)
			{
				target["extendedLifetimes"].push_back({
					{"resource", array.m_Address.m_Resource}, {"originalLastUser", array.m_OriginalLastUse == UINT32_MAX ? Json(nullptr) : Json(array.m_OriginalLastUse)},
					{"observedLastUser", array.m_ObservedLastUse} });
			}
			root["targets"].push_back(std::move(target));
		}
		return root.dump(2, ' ', false, Json::error_handler_t::replace);
	}

	std::string SerializeFrameGraphExport(const RGSnapshot& snapshot, const FrameCaptureMetadata& metadata,
		const ResourceExportSubmission& submission, bool summary, bool resourceExportActive)
	{
		Json root = Json::parse(SerializeRenderGraphSnapshot(snapshot, summary));
		root["frame"] = Frame(metadata, submission);
		root["frameSerial"] = metadata.m_FrameSerial;
		root["submission"] = Submission(submission);
		root["observation"] = { {"graph", true}, {"resourceExport", resourceExportActive}, {"nonFiniteWatch", false} };
		return root.dump(2, ' ', false, Json::error_handler_t::replace);
	}
}
