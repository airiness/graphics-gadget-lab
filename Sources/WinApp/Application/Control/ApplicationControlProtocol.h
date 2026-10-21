#pragma once
#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "Capture/NonFiniteWatchCoordinator.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace gglab
{
	// Version of the session control protocol: one UTF-8 JSON request line per
	// connection answered by one JSON response line. Increment it for every
	// change that renames, removes or reinterprets a field or command.
	inline constexpr uint32_t ApplicationControlProtocolVersion = 4;

	enum class ApplicationControlCommand : uint8_t
	{
		Status,
		Capture,
		Graph,
		Export,
		Watch,
		Result,
		CaptureCancel,
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
		// Capture/Graph/Export always answer admission immediately.
		CapturePlan m_Capture;
		// Result/CaptureCancel: process-local job identity, never a legacy output id.
		uint64_t m_JobId = 0;
		// Result: only terminal receipt consumption releases its reservation.
		bool m_Consume = false;
		// Sequence: the sequence to start.
		FrameSequenceRequest m_Sequence{};
		NonFiniteWatchConfiguration m_Watch{};
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
		CaptureIntakeSnapshot m_CaptureIntake;
		uint32_t m_SettledFrames = 0;
		// Absent before the first rendered frame.
		const FrameCaptureFrameState* m_Frame = nullptr;
		// The active or most recent sequence; absent before the first one.
		const FrameSequenceStatus* m_Sequence = nullptr;
		bool m_NonFiniteInstrumentation = false;
		std::shared_ptr<const NonFiniteProductionFrame> m_NonFiniteProduction;
		const NonFiniteWatchStatus* m_Watch = nullptr;
		// The process environment record as JSON; empty before the runtime services exist.
		std::string_view m_Environment;
	};

	// Response serializers. Every response carries the protocol version, the
	// request id and "ok"; failures add "error".
	[[nodiscard]] std::string SerializeApplicationControlError(
		uint64_t id, std::string_view error) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlStatus(
		uint64_t id, const ApplicationControlStatus& status) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlCaptureAdmission(
		uint64_t id, const CaptureAdmission& admission) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlCapturePending(
		uint64_t id, const CaptureJobSnapshot& snapshot) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlCaptureNotFound(
		uint64_t id, uint64_t jobId) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlCaptureResult(
		uint64_t id, const CaptureJobResult& result, bool consumed) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlCaptureCancellation(
		uint64_t id, uint64_t jobId, CaptureCancelResult cancellation) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlStopping(uint64_t id) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlWatch(uint64_t id,
		const NonFiniteWatchStatus& status, bool instrumentation) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlSequence(
		uint64_t id, const FrameSequenceStatus& status) noexcept;
	[[nodiscard]] std::string SerializeApplicationControlSequenceCancellation(uint64_t id,
		const FrameSequenceStatus* status, FrameSequenceCancelVerdict cancellation) noexcept;
}
