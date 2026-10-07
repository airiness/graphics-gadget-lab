#include "Application/SelfTest/ApplicationControlSelfTests.h"

#include "Application/Control/ApplicationControlProtocol.h"
#include "Application/Platform/Windows/Win32NamedPipeServer.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <windows.h>

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool Contains(std::string_view text, std::string_view fragment) noexcept
		{
			return text.find(fragment) != std::string_view::npos;
		}

		void RunParseTests(SelfTestContext& context) noexcept
		{
			const ApplicationControlParseResult status =
				ParseApplicationControlRequest(R"({"protocol":1,"id":7,"command":"status"})");
			context.Check(status.m_Request && status.m_Request->m_Id == 7 &&
				status.m_Request->m_Command == ApplicationControlCommand::Status,
				"A status request parses with its id");

			const ApplicationControlParseResult capture = ParseApplicationControlRequest(
				R"({"protocol":1,"id":8,"command":"capture","source":"composited",)"
				R"("timing":"next-frame","settleFrames":3,"requiredContentId":"gglab.lab.culling",)"
				R"("view":"CAM_Courtyard","outputDirectory":"D:/captures","label":"a","note":"b",)"
				R"("wait":false})");
			const bool captureParsed = capture.m_Request.has_value();
			context.Check(captureParsed &&
				capture.m_Request->m_Command == ApplicationControlCommand::Capture &&
				capture.m_Request->m_Capture.m_Source == FrameCaptureSource::Composited &&
				capture.m_Request->m_Capture.m_Timing == FrameCaptureTiming::NextFrame &&
				capture.m_Request->m_Capture.m_SettleFrames == 3 &&
				capture.m_Request->m_Capture.m_RequiredContentId == "gglab.lab.culling" &&
				capture.m_Request->m_Capture.m_ReferenceViewId == "CAM_Courtyard" &&
				capture.m_Request->m_Capture.m_OutputDirectory ==
				std::filesystem::path("D:/captures") &&
				capture.m_Request->m_Capture.m_Label == "a" &&
				capture.m_Request->m_Capture.m_Note == "b" && !capture.m_Request->m_Wait,
				"A capture request carries every optional capture field");

			const ApplicationControlParseResult defaults = ParseApplicationControlRequest(
				R"({"protocol":1,"id":9,"command":"capture"})");
			context.Check(defaults.m_Request &&
				defaults.m_Request->m_Capture.m_Timing == FrameCaptureTiming::AfterReady &&
				defaults.m_Request->m_Capture.m_SettleFrames == 8 &&
				defaults.m_Request->m_Capture.m_Source == FrameCaptureSource::Scene &&
				defaults.m_Request->m_Wait,
				"A bare capture waits for a settled after-ready scene capture");

			const ApplicationControlParseResult result = ParseApplicationControlRequest(
				R"({"protocol":1,"id":10,"command":"result","requestId":4})");
			context.Check(result.m_Request && result.m_Request->m_CaptureRequestId == 4,
				"A result request names one capture request");

			const ApplicationControlParseResult sequence = ParseApplicationControlRequest(
				R"({"protocol":1,"id":11,"command":"sequence","path":"SEQ_DollyDoorway",)"
				R"("requiredContentId":"Demo.CoastalAtrium","captureFrames":[0,90,179],)"
				R"("source":"composited","outputDirectory":"D:/captures","label":"dolly",)"
				R"("note":"baseline"})");
			context.Check(sequence.m_Request &&
				sequence.m_Request->m_Command == ApplicationControlCommand::Sequence &&
				sequence.m_Request->m_Sequence.m_CameraPathId == "SEQ_DollyDoorway" &&
				sequence.m_Request->m_Sequence.m_RequiredContentId == "Demo.CoastalAtrium" &&
				sequence.m_Request->m_Sequence.m_CaptureFrames ==
				std::vector<uint32_t>{ 0, 90, 179 } &&
				sequence.m_Request->m_Sequence.m_CaptureSource == FrameCaptureSource::Composited &&
				sequence.m_Request->m_Sequence.m_OutputDirectory ==
				std::filesystem::path("D:/captures") &&
				sequence.m_Request->m_Sequence.m_Label == "dolly" &&
				sequence.m_Request->m_Sequence.m_Note == "baseline",
				"A sequence request carries its path, capture frames and capture fields");
			const ApplicationControlParseResult cancel = ParseApplicationControlRequest(
				R"({"protocol":1,"id":12,"command":"sequence-cancel"})");
			context.Check(cancel.m_Request &&
				cancel.m_Request->m_Command == ApplicationControlCommand::SequenceCancel,
				"A sequence-cancel request takes no fields");

			struct Rejected
			{
				std::string_view m_Line;
				uint64_t m_Id;
			};
			const Rejected rejected[] = {
				{ "not json", 0 },
				{ R"([1,2])", 0 },
				{ R"({"protocol":1,"command":"status"})", 0 },
				{ R"({"protocol":2,"id":3,"command":"status"})", 3 },
				{ R"({"protocol":1,"id":4,"command":"reboot"})", 4 },
				{ R"({"protocol":1,"id":5,"command":"status","extra":1})", 5 },
				{ R"({"protocol":1,"id":6,"command":"capture","source":"hud"})", 6 },
				{ R"({"protocol":1,"id":7,"command":"capture","settleFrames":-1})", 7 },
				{ R"({"protocol":1,"id":8,"command":"capture","outputDirectory":"relative"})", 8 },
				{ R"({"protocol":1,"id":9,"command":"capture","lable":"typo"})", 9 },
				{ R"({"protocol":1,"id":10,"command":"result"})", 10 },
				{ R"({"protocol":1,"id":11,"command":"result","requestId":0})", 11 },
				{ R"({"protocol":1,"id":12,"command":"sequence"})", 12 },
				{ R"({"protocol":1,"id":13,"command":"sequence","path":"A","captureFrames":[-1]})", 13 },
				{ R"({"protocol":1,"id":14,"command":"sequence","path":"A","captureFrames":3})", 14 },
				{ R"({"protocol":1,"id":15,"command":"sequence","path":"A","frames":[1]})", 15 },
				{ R"({"protocol":1,"id":16,"command":"sequence-cancel","path":"A"})", 16 },
			};
			bool allRejected = true;
			for (const Rejected& entry : rejected)
			{
				const ApplicationControlParseResult parsed =
					ParseApplicationControlRequest(entry.m_Line);
				allRejected = allRejected && !parsed.m_Request && !parsed.m_Error.empty() &&
					parsed.m_Id == entry.m_Id;
			}
			context.Check(allRejected,
				"Malformed, unknown or mistyped requests fail with a reason and their id");
		}

		void RunSerializeTests(SelfTestContext& context) noexcept
		{
			const std::string error = SerializeApplicationControlError(3, "bad \"input\"");
			context.Check(Contains(error, R"("protocol":1)") && Contains(error, R"("id":3)") &&
				Contains(error, R"("ok":false)") && Contains(error, R"(bad \"input\")"),
				"Errors are versioned JSON with an escaped reason");

			FrameCaptureFrameState frame{
				.m_Backend = "vulkan",
				.m_DemoId = "Demo.LabHost",
				.m_LabId = "gglab.lab.culling",
				.m_FrameIndex = 42,
				.m_ReferenceViewIds = { "CAM_A", "CAM_B" },
			};
			frame.m_Readiness.Add("lab", FrameCaptureGateState::Pending, "Warming up.");
			const std::string status = SerializeApplicationControlStatus(4, {
				.m_SessionId = "s1",
				.m_ProcessId = 99,
				.m_Hidden = true,
				.m_Width = 640,
				.m_Height = 360,
				.m_UnfinishedCaptures = 2,
				.m_SettledFrames = 5,
				.m_Frame = &frame,
				});
			context.Check(Contains(status, R"("ok":true)") && Contains(status, R"("pid":99)") &&
				Contains(status, R"("labId":"gglab.lab.culling")") &&
				Contains(status, R"("ready":false)") && Contains(status, R"("settledFrames":5)") &&
				Contains(status, R"("unfinished":2)") && Contains(status, R"("state":"pending")") &&
				Contains(status, R"("fixedDeltaTime":null)") &&
				Contains(status, R"("referenceViews":["CAM_A","CAM_B"])"),
				"Status reports the session, pending captures, readiness gates and reference views");
			const std::string noFrame = SerializeApplicationControlStatus(5, { .m_SessionId = "s1" });
			context.Check(Contains(noFrame, R"("frame":null)") &&
				Contains(noFrame, R"("sequence":null)"),
				"Status before the first frame reports no frame state and no sequence");

			const FrameSequenceStatus sequenceStatus{
				.m_SequenceId = 3,
				.m_State = FrameSequenceState::Failed,
				.m_CameraPathId = "SEQ_DollyDoorway",
				.m_CameraPathVersion = 1,
				.m_FrameCount = 180,
				.m_SubmittedFrames = 12,
				.m_CaptureRequestIds = { 21, 22 },
				.m_CompletedCaptures = 1,
				.m_Failure = "Lost continuity.",
			};
			const std::string sequenceJson = SerializeApplicationControlSequence(10, sequenceStatus);
			const std::string statusWithSequence = SerializeApplicationControlStatus(11, {
				.m_SessionId = "s1",
				.m_Sequence = &sequenceStatus,
				});
			context.Check(Contains(sequenceJson, R"("state":"failed")") &&
				Contains(sequenceJson, R"("path":"SEQ_DollyDoorway")") &&
				Contains(sequenceJson, R"("frameCount":180)") &&
				Contains(sequenceJson, R"("captureRequestIds":[21,22])") &&
				Contains(sequenceJson, R"("failure":"Lost continuity.")") &&
				Contains(statusWithSequence, R"("submittedFrames":12)"),
				"Sequence responses and status report the sequence state, progress and failure");

			FrameCaptureRequestResult completed{
				.m_RequestId = 12,
				.m_Status = FrameCaptureRequestStatus::Completed,
				.m_ImagePath = std::filesystem::path(u8"D:/captures/a.png"),
				.m_MetadataPath = std::filesystem::path(u8"D:/captures/a.json"),
			};
			completed.m_Metadata = FrameCaptureMetadata{ .m_FrameSerial = 77 };
			const std::string completedJson = SerializeApplicationControlCaptureResult(6, completed);
			const std::string failedJson = SerializeApplicationControlCaptureResult(7, {
				.m_RequestId = 13,
				.m_Status = FrameCaptureRequestStatus::Failed,
				.m_Failure = "Device lost.",
				});
			context.Check(Contains(completedJson, R"("status":"completed")") &&
				Contains(completedJson, R"("image":"D:/captures/a.png")") &&
				Contains(completedJson, R"("frameSerial":77)") &&
				Contains(failedJson, R"("status":"failed")") &&
				Contains(failedJson, R"("failure":"Device lost.")") &&
				Contains(SerializeApplicationControlCaptureQueued(8, 14), R"("status":"queued")") &&
				Contains(SerializeApplicationControlStopping(9), R"("status":"stopping")"),
				"Capture results, queued captures and stop acknowledgements serialize their status");
		}

		[[nodiscard]] std::optional<std::string> SendPipeLine(
			const std::wstring& pipeName, std::string_view line) noexcept
		{
			HANDLE pipe = INVALID_HANDLE_VALUE;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (pipe == INVALID_HANDLE_VALUE && std::chrono::steady_clock::now() < deadline)
			{
				pipe = ::CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
					OPEN_EXISTING, 0, nullptr);
				if (pipe == INVALID_HANDLE_VALUE)
				{
					::WaitNamedPipeW(pipeName.c_str(), 100);
				}
			}
			if (pipe == INVALID_HANDLE_VALUE)
			{
				return std::nullopt;
			}
			std::string request(line);
			request.push_back('\n');
			DWORD bytes = 0;
			std::string response;
			if (::WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()), &bytes,
				nullptr))
			{
				char buffer[512];
				while (::ReadFile(pipe, buffer, sizeof(buffer), &bytes, nullptr) && bytes > 0)
				{
					response.append(buffer, bytes);
					if (response.find('\n') != std::string::npos)
					{
						break;
					}
				}
			}
			::CloseHandle(pipe);
			if (response.empty() || response.back() != '\n')
			{
				return std::nullopt;
			}
			response.pop_back();
			return response;
		}

		void RunPipeServerTests(SelfTestContext& context) noexcept
		{
			const std::wstring pipeName = std::format(L"\\\\.\\pipe\\gglab-selftest-{}-{}",
				::GetCurrentProcessId(),
				std::chrono::steady_clock::now().time_since_epoch().count());
			win32::NamedPipeServer server;
			win32::NamedPipeServer duplicate;
			const bool started = server.Start(pipeName, std::chrono::milliseconds(300));
			context.Check(started && !duplicate.Start(pipeName),
				"A control pipe name can be served by only one server");
			if (!started)
			{
				return;
			}

			// Two clients in flight at once: each request is answered independently.
			std::optional<std::string> firstResponse;
			std::optional<std::string> secondResponse;
			std::thread first([&]() { firstResponse = SendPipeLine(pipeName, "first"); });
			std::thread second([&]() { secondResponse = SendPipeLine(pipeName, "second\r"); });
			std::vector<std::shared_ptr<win32::NamedPipeRequest>> requests;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (requests.size() < 2 && std::chrono::steady_clock::now() < deadline)
			{
				for (auto& request : server.Poll())
				{
					requests.push_back(std::move(request));
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			for (const auto& request : requests)
			{
				request->Respond("echo:" + request->GetLine());
				request->Respond("ignored second response");
			}
			first.join();
			second.join();
			context.Check(requests.size() == 2 && firstResponse == "echo:first" &&
				secondResponse == "echo:second",
				"Concurrent clients each receive exactly the first response to their line");

			// A request still unanswered at stop receives the fallback response.
			std::optional<std::string> pendingResponse;
			std::thread pending([&]() { pendingResponse = SendPipeLine(pipeName, "pending"); });
			bool received = false;
			const auto pendingDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!received && std::chrono::steady_clock::now() < pendingDeadline)
			{
				received = !server.Poll().empty();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			// A client that never reads its response and keeps the pipe open holds
			// its connection only until the client close timeout.
			HANDLE silent = ::CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
				nullptr, OPEN_EXISTING, 0, nullptr);
			bool silentAnswered = false;
			if (silent != INVALID_HANDLE_VALUE)
			{
				constexpr std::string_view silentRequest = "silent\n";
				DWORD bytes = 0;
				::WriteFile(silent, silentRequest.data(), static_cast<DWORD>(silentRequest.size()),
					&bytes, nullptr);
				const auto silentDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
				while (!silentAnswered && std::chrono::steady_clock::now() < silentDeadline)
				{
					for (const auto& request : server.Poll())
					{
						request->Respond("unread");
						silentAnswered = true;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}

			const auto stopStart = std::chrono::steady_clock::now();
			server.Stop("stopped");
			const auto stopTime = std::chrono::steady_clock::now() - stopStart;
			pending.join();
			if (silent != INVALID_HANDLE_VALUE)
			{
				::CloseHandle(silent);
			}
			context.Check(received && pendingResponse == "stopped",
				"Stopping answers outstanding requests with the fallback response");
			context.Check(silentAnswered && stopTime < std::chrono::seconds(3),
				"A client that never reads its response cannot hold server shutdown");
		}
	}

	void RunApplicationControlSelfTests(SelfTestContext& context) noexcept
	{
		RunParseTests(context);
		RunSerializeTests(context);
		RunPipeServerTests(context);
	}
}
