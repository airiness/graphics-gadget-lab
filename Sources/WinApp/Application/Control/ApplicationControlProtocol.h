#pragma once
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameSequenceCoordinator.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gglab
{
	// Version of the session control protocol: one UTF-8 JSON request line per
	// connection answered by one JSON response line. Increment it for every
	// change that renames, removes or reinterprets a field or command.
	inline constexpr uint32_t ApplicationControlProtocolVersion = 1;

	enum class ApplicationControlCommand : uint8_t
	{
		Status,
		Capture,
		Result,
		Stop,
		// Starts a camera-path sequence; the response reports it without waiting.
		Sequence,
		// Cancels the active sequence.
		SequenceCancel,
	};

	struct ApplicationControlRequest
	{
		uint64_t m_Id = 0;
		ApplicationControlCommand m_Command = ApplicationControlCommand::Status;
		// Capture: the request to submit and whether the response waits for it.
		FrameCaptureRequest m_Capture{};
		bool m_Wait = true;
		// Result: the capture request to report.
		uint64_t m_CaptureRequestId = 0;
		// Sequence: the sequence to start.
		FrameSequenceRequest m_Sequence{};
	};

	struct ApplicationControlParseResult
	{
		std::optional<ApplicationControlRequest> m_Request;
		// Request id when it could be read, so errors can still be correlated.
		uint64_t m_Id = 0;
		std::string m_Error;
	};

	[[nodiscard]] ApplicationControlParseResult ParseApplicationControlRequest(
		std::string_view line) noexcept;

	struct ApplicationControlStatus
	{
		std::string m_SessionId;
		uint32_t m_ProcessId = 0;
		double m_UptimeSeconds = 0.0;
		bool m_Hidden = false;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_UnfinishedCaptures = 0;
		uint32_t m_SettledFrames = 0;
		// Absent before the first rendered frame.
		const FrameCaptureFrameState* m_Frame = nullptr;
		// The active or most recent sequence; absent before the first one.
		const FrameSequenceStatus* m_Sequence = nullptr;
	};

	// Response serializers. Every response carries the protocol version, the
	// request id and "ok"; failures add "error".
	[[nodiscard]] std::string SerializeApplicationControlError(
		uint64_t id, std::string_view error) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlStatus(
		uint64_t id, const ApplicationControlStatus& status) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlCaptureQueued(
		uint64_t id, uint64_t captureRequestId) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlCaptureResult(
		uint64_t id, const FrameCaptureRequestResult& result) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlStopping(uint64_t id) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlSequence(
		uint64_t id, const FrameSequenceStatus& status) noexcept;
}
