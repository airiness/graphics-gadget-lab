#include "Application/Control/ApplicationControlProtocol.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalReference.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace gglab
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr uint32_t MaxSettleFrames = 10000;
		constexpr size_t MaxSequenceCaptureFrames = 10000;

		[[nodiscard]] std::string ToUtf8(const std::filesystem::path& path)
		{
			const std::u8string text = path.u8string();
			return std::string(text.begin(), text.end());
		}

		[[nodiscard]] std::string Dump(const Json& document) noexcept
		{
			// Invalid UTF-8 in captured text is replaced instead of throwing.
			return document.dump(-1, ' ', false, Json::error_handler_t::replace);
		}

		[[nodiscard]] Json MakeResponse(uint64_t id, bool ok)
		{
			return Json{
				{ "protocol", ApplicationControlProtocolVersion },
				{ "id", id },
				{ "ok", ok },
			};
		}

		[[nodiscard]] std::optional<ApplicationControlCommand> ParseCommand(
			std::string_view command) noexcept
		{
			if (command == "status")
			{
				return ApplicationControlCommand::Status;
			}
			if (command == "capture")
			{
				return ApplicationControlCommand::Capture;
			}
			if (command == "result")
			{
				return ApplicationControlCommand::Result;
			}
			if (command == "stop")
			{
				return ApplicationControlCommand::Stop;
			}
			if (command == "sequence")
			{
				return ApplicationControlCommand::Sequence;
			}
			if (command == "sequence-cancel")
			{
				return ApplicationControlCommand::SequenceCancel;
			}
			return std::nullopt;
		}

		// Parses a diagnostic tap name; returns an error text.
		[[nodiscard]] std::string ParseDiagnosticTap(
			const Json& value, std::optional<PostProcessDebugTap>& outTap)
		{
			outTap = value.is_string()
				? FindFrameCaptureDiagnosticTap(value.get<std::string>())
				: std::nullopt;
			if (!outTap)
			{
				return "Field 'diagnosticTap' must name a diagnostic tap such as "
					"'temporal-history-weight'.";
			}
			return {};
		}

		[[nodiscard]] std::string ValidateDiagnosticTap(
			FrameCaptureSource source, const std::optional<PostProcessDebugTap>& tap)
		{
			if ((source == FrameCaptureSource::Diagnostic) != tap.has_value())
			{
				return "Field 'diagnosticTap' is required for, and only valid with, source "
					"'diagnostic'.";
			}
			return {};
		}

		// Parses an absolute output directory; returns an error text.
		[[nodiscard]] std::string ParseOutputDirectory(
			const Json& value, std::filesystem::path& outDirectory)
		{
			if (!value.is_string())
			{
				return "Field 'outputDirectory' must be a string.";
			}
			const std::string text = value.get<std::string>();
			outDirectory = std::filesystem::path(std::u8string(text.begin(), text.end()));
			if (!outDirectory.is_absolute())
			{
				return "Field 'outputDirectory' must be an absolute directory.";
			}
			return {};
		}

		// Parses the Temporal AA overrides of a sequence; returns an error text. Values
		// outside a setting's range are rejected instead of clamped, so a run records
		// exactly the configuration it was asked to evaluate.
		[[nodiscard]] std::string ParseTemporalAAOverrides(
			const Json& value, FrameSequenceTemporalAAOverrides& outOverrides)
		{
			if (!value.is_object())
			{
				return "Field 'temporalAA' must be an object.";
			}
			struct OverrideField
			{
				std::string_view m_Name;
				std::optional<float> FrameSequenceTemporalAAOverrides::* m_Member;
				float m_Max;
			};
			constexpr std::array<OverrideField, 6> fields{ {
				{ "maxHistoryFeedback", &FrameSequenceTemporalAAOverrides::m_MaxHistoryFeedback,
					TemporalAAMaxHistoryFeedbackCeiling },
				{ "depthAbsoluteThreshold",
					&FrameSequenceTemporalAAOverrides::m_DepthAbsoluteThreshold,
					TemporalAAMaxDepthThreshold },
				{ "depthRelativeThreshold",
					&FrameSequenceTemporalAAOverrides::m_DepthRelativeThreshold,
					TemporalAAMaxDepthThreshold },
				{ "velocityWeightScale", &FrameSequenceTemporalAAOverrides::m_VelocityWeightScale,
					TemporalAAMaxVelocityWeightScale },
				{ "luminanceWeightScale",
					&FrameSequenceTemporalAAOverrides::m_LuminanceWeightScale,
					TemporalAAMaxLuminanceWeightScale },
				{ "neighborhoodClampExpansion",
					&FrameSequenceTemporalAAOverrides::m_NeighborhoodClampExpansion,
					TemporalAAMaxNeighborhoodClampExpansion },
			} };
			for (const auto& [key, fieldValue] : value.items())
			{
				if (key == "historyFilter")
				{
					const std::string name =
						fieldValue.is_string() ? fieldValue.get<std::string>() : "";
					constexpr std::array filters{ TemporalAAHistoryFilter::Bilinear,
						TemporalAAHistoryFilter::CatmullRomClamped };
					const auto filter = std::ranges::find(
						filters, name, &GetTemporalAAHistoryFilterName);
					if (filter == filters.end())
					{
						return "Temporal AA override 'historyFilter' must be 'bilinear' or "
							"'catmull-rom-clamped'.";
					}
					outOverrides.m_HistoryFilter = *filter;
					continue;
				}
				if (key == "currentFilter")
				{
					const std::string name =
						fieldValue.is_string() ? fieldValue.get<std::string>() : "";
					constexpr std::array filters{ TemporalAACurrentFilter::Point,
						TemporalAACurrentFilter::Gaussian };
					const auto filter = std::ranges::find(
						filters, name, &GetTemporalAACurrentFilterName);
					if (filter == filters.end())
					{
						return "Temporal AA override 'currentFilter' must be 'point' or "
							"'gaussian'.";
					}
					outOverrides.m_CurrentFilter = *filter;
					continue;
				}
				if (key == "motionSelection")
				{
					const std::string name =
						fieldValue.is_string() ? fieldValue.get<std::string>() : "";
					constexpr std::array selections{ TemporalAAMotionSelection::Center,
						TemporalAAMotionSelection::ClosestDepth };
					const auto selection = std::ranges::find(
						selections, name, &GetTemporalAAMotionSelectionName);
					if (selection == selections.end())
					{
						return "Temporal AA override 'motionSelection' must be 'center' or "
							"'closest-depth'.";
					}
					outOverrides.m_MotionSelection = *selection;
					continue;
				}
				const auto field = std::ranges::find(fields, key, &OverrideField::m_Name);
				if (field == fields.end())
				{
					return std::format("Unknown Temporal AA override '{}'.", key);
				}
				const double number = fieldValue.is_number() ? fieldValue.get<double>() : -1.0;
				if (!fieldValue.is_number() || !std::isfinite(number) || number < 0.0 ||
					number > static_cast<double>(field->m_Max))
				{
					return std::format("Temporal AA override '{}' must be a number from 0 to {}.",
						key, field->m_Max);
				}
				outOverrides.*(field->m_Member) = static_cast<float>(number);
			}
			return {};
		}

		// Fills the sequence request; returns an error text.
		[[nodiscard]] std::string ParseSequenceFields(
			const Json& document, ApplicationControlRequest& request)
		{
			FrameSequenceRequest& sequence = request.m_Sequence;
			for (const auto& [key, value] : document.items())
			{
				if (key == "protocol" || key == "id" || key == "command")
				{
					continue;
				}
				if (key == "path" || key == "requiredContentId" || key == "label" ||
					key == "note")
				{
					if (!value.is_string())
					{
						return std::format("Field '{}' must be a string.", key);
					}
					std::string& target = key == "path" ? sequence.m_CameraPathId
						: key == "requiredContentId" ? sequence.m_RequiredContentId
						: key == "label" ? sequence.m_Label
						: sequence.m_Note;
					target = value.get<std::string>();
				}
				else if (key == "source")
				{
					const std::string source = value.is_string() ? value.get<std::string>() : "";
					if (source == "scene")
					{
						sequence.m_CaptureSource = FrameCaptureSource::Scene;
					}
					else if (source == "composited")
					{
						sequence.m_CaptureSource = FrameCaptureSource::Composited;
					}
					else if (source == "diagnostic")
					{
						sequence.m_CaptureSource = FrameCaptureSource::Diagnostic;
					}
					else
					{
						return "Field 'source' must be 'scene', 'composited' or 'diagnostic'.";
					}
				}
				else if (key == "diagnosticTap")
				{
					if (std::string error = ParseDiagnosticTap(value, sequence.m_DiagnosticTap);
						!error.empty())
					{
						return error;
					}
				}
				else if (key == "referenceSamples")
				{
					if (!value.is_number_unsigned() ||
						value.get<uint64_t>() > MaxTemporalReferenceSamples)
					{
						return std::format("Field 'referenceSamples' must be 0 to {}.",
							MaxTemporalReferenceSamples);
					}
					sequence.m_ReferenceSamples = value.get<uint32_t>();
				}
				else if (key == "captureFrames")
				{
					if (!value.is_array() || value.size() > MaxSequenceCaptureFrames)
					{
						return std::format("Field 'captureFrames' must be an array of at most {} "
							"frame numbers.", MaxSequenceCaptureFrames);
					}
					for (const Json& frame : value)
					{
						if (!frame.is_number_unsigned() ||
							frame.get<uint64_t>() > std::numeric_limits<uint32_t>::max())
						{
							return "Field 'captureFrames' must contain unsigned 32-bit frame numbers.";
						}
						sequence.m_CaptureFrames.push_back(frame.get<uint32_t>());
					}
				}
				else if (key == "outputDirectory")
				{
					if (std::string error = ParseOutputDirectory(value, sequence.m_OutputDirectory);
						!error.empty())
					{
						return error;
					}
				}
				else if (key == "temporalAA")
				{
					if (std::string error =
						ParseTemporalAAOverrides(value, sequence.m_TemporalAAOverrides);
						!error.empty())
					{
						return error;
					}
				}
				else if (key == "gpuTiming")
				{
					if (!value.is_boolean())
					{
						return "Field 'gpuTiming' must be a boolean.";
					}
					sequence.m_GpuTiming = value.get<bool>();
				}
				else
				{
					return std::format("Unknown field '{}' for command 'sequence'.", key);
				}
			}
			if (sequence.m_CameraPathId.empty())
			{
				return "Command 'sequence' requires a non-empty string 'path'.";
			}
			return ValidateDiagnosticTap(sequence.m_CaptureSource, sequence.m_DiagnosticTap);
		}

		// Fills the capture request from optional fields; returns an error text.
		[[nodiscard]] std::string ParseCaptureFields(
			const Json& document, ApplicationControlRequest& request)
		{
			FrameCaptureRequest& capture = request.m_Capture;
			capture.m_Timing = FrameCaptureTiming::AfterReady;
			capture.m_SettleFrames = 8;
			for (const auto& [key, value] : document.items())
			{
				if (key == "protocol" || key == "id" || key == "command")
				{
					continue;
				}
				if (key == "source")
				{
					const std::string source = value.is_string() ? value.get<std::string>() : "";
					if (source == "scene")
					{
						capture.m_Source = FrameCaptureSource::Scene;
					}
					else if (source == "composited")
					{
						capture.m_Source = FrameCaptureSource::Composited;
					}
					else if (source == "diagnostic")
					{
						capture.m_Source = FrameCaptureSource::Diagnostic;
					}
					else
					{
						return "Field 'source' must be 'scene', 'composited' or 'diagnostic'.";
					}
				}
				else if (key == "diagnosticTap")
				{
					if (std::string error = ParseDiagnosticTap(value, capture.m_DiagnosticTap);
						!error.empty())
					{
						return error;
					}
				}
				else if (key == "timing")
				{
					const std::string timing = value.is_string() ? value.get<std::string>() : "";
					if (timing == "after-ready")
					{
						capture.m_Timing = FrameCaptureTiming::AfterReady;
					}
					else if (timing == "next-frame")
					{
						capture.m_Timing = FrameCaptureTiming::NextFrame;
					}
					else
					{
						return "Field 'timing' must be 'after-ready' or 'next-frame'.";
					}
				}
				else if (key == "settleFrames")
				{
					if (!value.is_number_unsigned() || value.get<uint64_t>() > MaxSettleFrames)
					{
						return std::format("Field 'settleFrames' must be 0 to {}.", MaxSettleFrames);
					}
					capture.m_SettleFrames = value.get<uint32_t>();
				}
				else if (key == "requiredContentId" || key == "view" || key == "label" ||
					key == "note")
				{
					if (!value.is_string())
					{
						return std::format("Field '{}' must be a string.", key);
					}
					std::string& target = key == "requiredContentId" ? capture.m_RequiredContentId
						: key == "view" ? capture.m_ReferenceViewId
						: key == "label" ? capture.m_Label
						: capture.m_Note;
					target = value.get<std::string>();
				}
				else if (key == "outputDirectory")
				{
					if (std::string error = ParseOutputDirectory(value, capture.m_OutputDirectory);
						!error.empty())
					{
						return error;
					}
				}
				else if (key == "wait")
				{
					if (!value.is_boolean())
					{
						return "Field 'wait' must be a boolean.";
					}
					request.m_Wait = value.get<bool>();
				}
				else
				{
					return std::format("Unknown field '{}' for command 'capture'.", key);
				}
			}
			return ValidateDiagnosticTap(capture.m_Source, capture.m_DiagnosticTap);
		}

		[[nodiscard]] Json SerializeGates(const FrameCaptureReadiness& readiness)
		{
			Json gates = Json::array();
			for (const FrameCaptureGate& gate : readiness.m_Gates)
			{
				gates.push_back({
					{ "name", gate.m_Name },
					{ "state", GetFrameCaptureGateStateName(gate.m_State) },
					{ "detail", gate.m_Detail },
					});
			}
			return gates;
		}

		[[nodiscard]] Json SerializeTimingSummary(std::span<const double> milliseconds)
		{
			const FrameSequenceTimingSummary summary = SummarizeFrameSequenceTiming(milliseconds);
			return Json{
				{ "count", summary.m_Count },
				{ "meanMs", summary.m_Mean },
				{ "medianMs", summary.m_Median },
				{ "p90Ms", summary.m_P90 },
				{ "minMs", summary.m_Min },
				{ "maxMs", summary.m_Max },
			};
		}

		[[nodiscard]] Json SerializeGpuTiming(const FrameSequenceGpuTiming& timing)
		{
			Json scopes = Json::array();
			for (const FrameSequenceGpuTimingSeries& series : timing.m_Scopes)
			{
				Json scope = SerializeTimingSummary(series.m_Milliseconds);
				scope["name"] = series.m_Name;
				scopes.push_back(std::move(scope));
			}
			return Json{
				{ "frames", timing.m_FrameMilliseconds.size() },
				{ "frame", SerializeTimingSummary(timing.m_FrameMilliseconds) },
				{ "scopes", std::move(scopes) },
			};
		}

		[[nodiscard]] Json SerializeSequence(const FrameSequenceStatus& status)
		{
			Json sequence = {
				{ "id", status.m_SequenceId },
				{ "state", GetFrameSequenceStateName(status.m_State) },
				{ "path", status.m_CameraPathId },
				{ "pathVersion", status.m_CameraPathVersion },
				{ "frameCount", status.m_FrameCount },
				{ "referenceSamples", status.m_ReferenceSamples },
				{ "submittedFrames", status.m_SubmittedFrames },
				{ "captureRequestIds", status.m_CaptureRequestIds },
				{ "completedCaptures", status.m_CompletedCaptures },
			};
			if (status.m_GpuTiming)
			{
				sequence["gpuTiming"] = SerializeGpuTiming(*status.m_GpuTiming);
			}
			if (!status.m_Failure.empty())
			{
				sequence["failure"] = status.m_Failure;
			}
			return sequence;
		}
	}

	ApplicationControlParseResult ParseApplicationControlRequest(std::string_view line) noexcept
	{
		ApplicationControlParseResult result{};
		const Json document = Json::parse(line, nullptr, false);
		if (document.is_discarded() || !document.is_object())
		{
			result.m_Error = "The request is not a JSON object.";
			return result;
		}

		const auto id = document.find("id");
		if (id == document.end() || !id->is_number_unsigned())
		{
			result.m_Error = "Field 'id' must be an unsigned integer.";
			return result;
		}
		result.m_Id = id->get<uint64_t>();

		const auto protocol = document.find("protocol");
		if (protocol == document.end() || !protocol->is_number_unsigned() ||
			protocol->get<uint64_t>() != ApplicationControlProtocolVersion)
		{
			result.m_Error = std::format(
				"Field 'protocol' must be {}.", ApplicationControlProtocolVersion);
			return result;
		}

		const auto commandField = document.find("command");
		const std::optional<ApplicationControlCommand> command =
			commandField != document.end() && commandField->is_string()
			? ParseCommand(commandField->get<std::string>())
			: std::nullopt;
		if (!command)
		{
			result.m_Error = "Field 'command' must be 'status', 'capture', 'result', 'stop', "
				"'sequence' or 'sequence-cancel'.";
			return result;
		}

		ApplicationControlRequest request{ .m_Id = result.m_Id, .m_Command = *command };
		std::string error;
		switch (*command)
		{
		case ApplicationControlCommand::Capture:
			error = ParseCaptureFields(document, request);
			break;
		case ApplicationControlCommand::Result:
		{
			const auto requestId = document.find("requestId");
			if (requestId == document.end() || !requestId->is_number_unsigned() ||
				requestId->get<uint64_t>() == 0 || document.size() != 4)
			{
				error = "Command 'result' takes exactly one non-zero unsigned 'requestId'.";
				break;
			}
			request.m_CaptureRequestId = requestId->get<uint64_t>();
			break;
		}
		case ApplicationControlCommand::Sequence:
			error = ParseSequenceFields(document, request);
			break;
		case ApplicationControlCommand::Status:
		case ApplicationControlCommand::Stop:
		case ApplicationControlCommand::SequenceCancel:
			if (document.size() != 3)
			{
				error = std::format("Command '{}' takes no fields.",
					commandField->get<std::string>());
			}
			break;
		}
		if (!error.empty())
		{
			result.m_Error = std::move(error);
			return result;
		}
		result.m_Request = std::move(request);
		return result;
	}

	std::string SerializeApplicationControlError(uint64_t id, std::string_view error) noexcept
	{
		Json response = MakeResponse(id, false);
		response["error"] = std::string(error);
		return Dump(response);
	}

	std::string SerializeApplicationControlStatus(
		uint64_t id, const ApplicationControlStatus& status) noexcept
	{
		Json response = MakeResponse(id, true);
		response["session"] = {
			{ "id", status.m_SessionId },
			{ "pid", status.m_ProcessId },
			{ "uptimeSeconds", status.m_UptimeSeconds },
			{ "hidden", status.m_Hidden },
			{ "width", status.m_Width },
			{ "height", status.m_Height },
		};
		response["capture"] = { { "unfinished", status.m_UnfinishedCaptures } };
		response["sequence"] = status.m_Sequence ? SerializeSequence(*status.m_Sequence) : Json();
		if (!status.m_Frame)
		{
			response["frame"] = nullptr;
			return Dump(response);
		}

		const FrameCaptureFrameState& frame = *status.m_Frame;
		const FrameCaptureCameraState& camera = frame.m_Camera;
		response["frame"] = {
			{ "backend", frame.m_Backend },
			{ "demoId", frame.m_DemoId },
			{ "labId", frame.m_LabId },
			{ "frameIndex", frame.m_FrameIndex },
			{ "ready", frame.m_Readiness.IsReady() },
			{ "settledFrames", status.m_SettledFrames },
			{ "gates", SerializeGates(frame.m_Readiness) },
			{ "camera", {
				{ "name", camera.m_Name },
				{ "position", camera.m_Position },
				{ "forward", camera.m_Forward },
				{ "verticalFovDegrees", camera.m_VerticalFovDegrees },
			} },
			{ "referenceViews", frame.m_ReferenceViewIds },
			{ "fixedDeltaTime", frame.m_FixedDeltaTime ? Json(*frame.m_FixedDeltaTime) : Json() },
		};
		return Dump(response);
	}

	std::string SerializeApplicationControlCaptureQueued(
		uint64_t id, uint64_t captureRequestId) noexcept
	{
		Json response = MakeResponse(id, true);
		response["requestId"] = captureRequestId;
		response["status"] = "queued";
		return Dump(response);
	}

	std::string SerializeApplicationControlCaptureResult(
		uint64_t id, const FrameCaptureRequestResult& result) noexcept
	{
		Json response = MakeResponse(id, true);
		response["requestId"] = result.m_RequestId;
		switch (result.m_Status)
		{
		case FrameCaptureRequestStatus::Completed:
			response["status"] = "completed";
			response["image"] = ToUtf8(result.m_ImagePath);
			response["metadata"] = ToUtf8(result.m_MetadataPath);
			break;
		case FrameCaptureRequestStatus::Cancelled:
			response["status"] = "cancelled";
			response["failure"] = result.m_Failure;
			break;
		case FrameCaptureRequestStatus::Failed:
			response["status"] = "failed";
			response["failure"] = result.m_Failure;
			break;
		}
		if (result.m_Metadata)
		{
			response["frameSerial"] = result.m_Metadata->m_FrameSerial;
		}
		return Dump(response);
	}

	std::string SerializeApplicationControlStopping(uint64_t id) noexcept
	{
		Json response = MakeResponse(id, true);
		response["status"] = "stopping";
		return Dump(response);
	}

	std::string SerializeApplicationControlSequence(
		uint64_t id, const FrameSequenceStatus& status) noexcept
	{
		Json response = MakeResponse(id, true);
		response["sequence"] = SerializeSequence(status);
		return Dump(response);
	}
}
