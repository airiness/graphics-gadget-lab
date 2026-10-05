#include "Application/Control/ApplicationControlProtocol.h"

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace gglab
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr uint32_t MaxSettleFrames = 10000;

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
			return std::nullopt;
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
					else
					{
						return "Field 'source' must be 'scene' or 'composited'.";
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
					if (!value.is_string())
					{
						return "Field 'outputDirectory' must be a string.";
					}
					const std::string text = value.get<std::string>();
					capture.m_OutputDirectory = std::filesystem::path(
						std::u8string(text.begin(), text.end()));
					if (!capture.m_OutputDirectory.is_absolute())
					{
						return "Field 'outputDirectory' must be an absolute directory.";
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
			return {};
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
			result.m_Error = "Field 'command' must be 'status', 'capture', 'result' or 'stop'.";
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
		case ApplicationControlCommand::Status:
		case ApplicationControlCommand::Stop:
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
}
