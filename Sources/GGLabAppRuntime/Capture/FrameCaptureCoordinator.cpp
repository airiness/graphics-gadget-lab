#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/ResourceExportWriter.h"

#include "AppRuntimeLog.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <format>
#include <fstream>
#include <limits>
#include <mutex>
#include <random>
#include <span>
#include <system_error>
#include <thread>
#include <utility>

namespace gglab
{
	namespace
	{
		enum class EncodeStage : uint32_t
		{
			Queued,
			Encoding,
			SerializingGraph,
			WritingGraph,
			WritingImage,
			WritingMetadata,
			Publishing,
			Done,
		};

		[[nodiscard]] const char* GetEncodeStageName(EncodeStage stage) noexcept
		{
			switch (stage)
			{
			case EncodeStage::Queued:
				return "queued";
			case EncodeStage::Encoding:
				return "encoding";
			case EncodeStage::SerializingGraph:
				return "serializing graph";
			case EncodeStage::WritingGraph:
				return "writing graph";
			case EncodeStage::WritingImage:
				return "writing image";
			case EncodeStage::WritingMetadata:
				return "writing metadata";
			case EncodeStage::Publishing:
				return "publishing";
			case EncodeStage::Done:
				return "done";
			}
			return "unknown";
		}

		// Decides whether a job's files may appear. Shutdown can abandon a job
		// only before the writer claims publication; abandoned jobs never publish.
		// If the writer claims publication first, its files may already exist or
		// appear later even when shutdown reports the unfinished job as failed.
		enum class PublicationClaim : uint8_t
		{
			Open,
			Publishing,
			Abandoned,
		};
	}

	struct FrameCaptureCoordinator::EncodeJob
	{
		std::shared_ptr<const FrameCaptureImage> m_Image;
		// Immutable JSON prepared from the fenced Runtime record on the writer thread.
		std::shared_ptr<const RGSnapshot> m_Graph;
		std::shared_ptr<const ResourceExportPayload> m_Resources;
		bool m_ResourceExportActive = false;
		bool m_GraphSummary = false;
		uint32_t m_SubmissionFenceIndex = 0;
		uint32_t m_SubmissionFenceGeneration = 0;
		uint64_t m_SubmissionFenceValue = 0;
		std::string m_AdapterCompatibilityIdentity;
		std::filesystem::path m_Directory;
		// Preferred file stem; the writer appends a suffix when it is taken.
		std::string m_BaseStem;
		// Unique across processes, so concurrent writers never share a
		// temporary file.
		std::string m_TemporaryStem;
		// The writer records the published image file name before serializing.
		FrameCaptureMetadata m_Metadata;
		// Published paths, set by the writer before it marks the job done.
		std::filesystem::path m_ImagePath;
		std::filesystem::path m_MetadataPath;
		std::vector<std::filesystem::path> m_ResourcePaths;
		std::filesystem::path m_GraphPath;

		// Progress is published by the writer for stall and shutdown reports.
		std::atomic<EncodeStage> m_Stage = EncodeStage::Queued;
		std::atomic<PublicationClaim> m_Claim = PublicationClaim::Open;
		std::chrono::steady_clock::time_point m_SubmittedAt{};
		bool m_StallReported = false;

		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		bool m_IsDone = false;
		std::string m_Failure;
	};

	// The writer thread holds its own reference, so a writer left blocked in file
	// I/O at shutdown never touches the destroyed coordinator.
	struct FrameCaptureCoordinator::WriterState
	{
		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		std::deque<std::shared_ptr<EncodeJob>> m_Queue;
		bool m_Stopping = false;
	};

	namespace
	{
		// An encode normally finishes well within a second; a longer one is reported.
		constexpr std::chrono::seconds EncodeStallReportTime{ 10 };
		constexpr size_t MaxFileStemComponentLength = 64;

		// Empty when no gate failed.
		[[nodiscard]] std::string DescribeFailedGates(const FrameCaptureReadiness& readiness)
		{
			std::string failure;
			for (const FrameCaptureGate& gate : readiness.m_Gates)
			{
				if (gate.m_State != FrameCaptureGateState::Failed)
				{
					continue;
				}
				failure += failure.empty() ? "Readiness failed: " : "; ";
				failure += gate.m_Detail.empty() ? gate.m_Name
					: std::format("{} ({})", gate.m_Name, gate.m_Detail);
			}
			if (!failure.empty())
			{
				failure += '.';
			}
			return failure;
		}

		// A capture keeps its preferred name unless another capture, possibly of
		// another process, already published it.
		constexpr uint32_t MaxPublishAttempts = 1000;

		[[nodiscard]] std::string CreateTemporaryTag() noexcept
		{
			uint64_t value = static_cast<uint64_t>(
				std::chrono::steady_clock::now().time_since_epoch().count());
			try
			{
				std::random_device device;
				value ^= (static_cast<uint64_t>(device()) << 32) | device();
			}
			catch (...)
			{
			}
			return std::format("{:016x}", value);
		}

		// Writes the whole file, which is published only once it is complete.
		[[nodiscard]] std::string WriteFileContents(
			const std::filesystem::path& path, std::span<const uint8_t> bytes,
			std::span<const uint8_t> tail = {}) noexcept
		{
			std::error_code errorCode;
			std::filesystem::create_directories(path.parent_path(), errorCode);
			if (!std::filesystem::is_directory(path.parent_path(), errorCode))
			{
				return std::format("The output directory '{}' could not be created.",
					GetFrameCapturePathText(path.parent_path()));
			}

			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
			if (!tail.empty())
			{
				file.write(reinterpret_cast<const char*>(tail.data()), static_cast<std::streamsize>(tail.size()));
			}
			file.close();
			if (!file)
			{
				std::filesystem::remove(path, errorCode);
				return std::format("Writing '{}' failed.", GetFrameCapturePathText(path));
			}
			return {};
		}

		enum class PublishOutcome : uint8_t
		{
			Published,
			NameTaken,
			Failed,
		};

		// Publishes a complete temporary file under a name that must not exist yet.
		// A hard link creates the name only if it is free, so a file of another
		// writer is never replaced. File systems without hard links fall back to a
		// rename after checking the name, which a concurrent writer can still race.
		[[nodiscard]] PublishOutcome PublishNewFile(
			const std::filesystem::path& temporaryPath, const std::filesystem::path& path) noexcept
		{
			std::error_code errorCode;
			std::filesystem::create_hard_link(temporaryPath, path, errorCode);
			if (!errorCode)
			{
				return PublishOutcome::Published;
			}
			if (errorCode == std::errc::file_exists)
			{
				return PublishOutcome::NameTaken;
			}
			std::error_code existsError;
			if (std::filesystem::exists(path, existsError))
			{
				return PublishOutcome::NameTaken;
			}
			std::filesystem::rename(temporaryPath, path, errorCode);
			return errorCode ? PublishOutcome::Failed : PublishOutcome::Published;
		}

		[[nodiscard]] std::string SanitizeFileStemComponent(std::string_view value) noexcept
		{
			std::string result;
			for (const char character : value)
			{
				if (result.size() >= MaxFileStemComponentLength)
				{
					break;
				}
				const bool allowed = (character >= 'a' && character <= 'z') ||
					(character >= 'A' && character <= 'Z') ||
					(character >= '0' && character <= '9') || character == '-' ||
					character == '_' || character == '.';
				result += allowed ? character : '_';
			}
			return result.empty() ? std::string("capture") : result;
		}
	}

	FrameCaptureCoordinator::FrameCaptureCoordinator(const CreateInfo& createInfo) noexcept :
		m_Capture(createInfo.m_Capture),
		m_DefaultOutputDirectory(createInfo.m_DefaultOutputDirectory),
		m_ImageEncoder(createInfo.m_ImageEncoder),
		m_ShutdownWriteTimeout(createInfo.m_ShutdownWriteTimeout),
		m_WriteOnCallingThread(createInfo.m_WriteOnCallingThread),
		m_TemporaryTag(CreateTemporaryTag())
	{
		if (m_WriteOnCallingThread)
		{
			return;
		}
		try
		{
			m_Writer = std::make_shared<WriterState>();
			m_WriterThread = std::thread(&FrameCaptureCoordinator::RunWriter, m_Writer, m_ImageEncoder);
		}
		catch (...)
		{
			GGLAB_LOG_WARN_ALWAYS(
				"The frame capture writer thread could not start; captures are written on the frame thread.");
			m_Writer.reset();
			m_WriteOnCallingThread = true;
		}
	}

	FrameCaptureCoordinator::~FrameCaptureCoordinator()
	{
		FinishWriting();
		StopWriter();
	}

	uint64_t FrameCaptureCoordinator::Submit(FrameCaptureRequest request) noexcept
	{
		Entry& entry = m_Entries.emplace_back();
		entry.m_Id = m_NextRequestId++;
		entry.m_Request = std::move(request);
		if (m_IsShuttingDown)
		{
			Finish(entry, FrameCaptureRequestStatus::Cancelled,
				"The runtime is shutting down.");
		}
		return entry.m_Id;
	}

	bool FrameCaptureCoordinator::Cancel(uint64_t requestId) noexcept
	{
		for (Entry& entry : m_Entries)
		{
			if (entry.m_Id == requestId && entry.m_Phase == Phase::Waiting)
			{
				Finish(entry, FrameCaptureRequestStatus::Cancelled, "Cancelled by request.");
				return true;
			}
		}
		return false;
	}

	void FrameCaptureCoordinator::ConsumeResults(
		std::vector<FrameCaptureRequestResult>& outResults) noexcept
	{
		RemoveFinishedEntries();
		outResults.insert(outResults.end(), std::make_move_iterator(m_Results.begin()),
			std::make_move_iterator(m_Results.end()));
		m_Results.clear();
	}

	uint32_t FrameCaptureCoordinator::GetUnfinishedRequestCount() const noexcept
	{
		return static_cast<uint32_t>(std::ranges::count_if(m_Entries,
			[](const Entry& entry) { return entry.m_Phase != Phase::Finished; }));
	}

	const FrameCaptureFrameState* FrameCaptureCoordinator::GetLastFrameState() const noexcept
	{
		return m_LastFrameState ? &*m_LastFrameState : nullptr;
	}

	FrameCaptureAvailability FrameCaptureCoordinator::GetCaptureAvailability(FrameCaptureSource source)
		const noexcept
	{
		if (!m_Capture || m_IsShuttingDown)
		{
			return FrameCaptureAvailability::Finalized;
		}
		return m_LastFrameState ? GetCaptureAvailability(source, *m_LastFrameState)
			: FrameCaptureAvailability::Available;
	}

	FrameCaptureAvailability FrameCaptureCoordinator::GetCaptureAvailability(FrameCaptureSource source,
		const FrameCaptureFrameState& state) const noexcept
	{
		if (!m_Capture || m_IsShuttingDown)
		{
			return FrameCaptureAvailability::Finalized;
		}
		// A write slot is reserved before GPU work and stays reserved through encoding.
		// Counting only queued writer jobs misses issued captures still on the GPU.
		const auto reserved = std::ranges::count_if(m_Entries, [](const Entry& entry)
			{
				return entry.m_Phase == Phase::Issued || entry.m_Phase == Phase::Encoding;
			});
		if (reserved >= MaxInFlightCaptures)
		{
			return FrameCaptureAvailability::Busy;
		}
		if (source == FrameCaptureSource::RenderGraph || source == FrameCaptureSource::ResourceExport)
		{
			return source == FrameCaptureSource::ResourceExport ? m_Capture->GetResourceExportFrameAvailability()
				: m_Capture->GetRequestAvailability();
		}
		const ViewExtent extent = state.m_SettleKey.m_Temporal.m_Identity.m_ColorExtent;
		return m_Capture->GetAvailability(source == FrameCaptureSource::Diagnostic
			? RHIFormat::R8G8B8A8Unorm : state.m_DisplayFormat,
			extent.m_Width, extent.m_Height);
	}

	void FrameCaptureCoordinator::BeginFrame(FrameCaptureFrameState state) noexcept
	{
		const bool ready = state.m_Readiness.IsReady();
		const bool historyRestarted =
			!m_LastFrameState || !state.m_SettleKey.Continues(m_LastFrameState->m_SettleKey);
		// Temporal history keeps accumulating while gates are pending, so frames of
		// content still loading stay in it until the settle key changes.
		m_HistoryReady = historyRestarted ? ready : m_HistoryReady && ready;
		if (!m_HistoryReady || historyRestarted)
		{
			m_SettledFrames = 0;
		}
		for (Entry& entry : m_Entries)
		{
			if (entry.m_Phase != Phase::Waiting || !entry.m_ViewApplied)
			{
				continue;
			}
			if (!entry.m_ViewCameraResetSerial)
			{
				entry.m_ViewCameraResetSerial = state.m_SettleKey.m_Temporal.m_Identity.m_ResetIdentity;
			}
			else if (*entry.m_ViewCameraResetSerial != state.m_SettleKey.m_Temporal.m_Identity.m_ResetIdentity)
			{
				entry.m_ViewApplied = false;
				entry.m_ViewCameraResetSerial.reset();
			}
		}
		m_HasOpenFrame = true;

		if (!m_IsShuttingDown)
		{
			std::optional<PostProcessDebugTap> diagnosticTap =
				m_Capture ? m_Capture->GetPendingDiagnosticTap() : std::nullopt;
			const std::string readinessFailure = DescribeFailedGates(state.m_Readiness);
			for (Entry& entry : m_Entries)
			{
				if (entry.m_Phase != Phase::Waiting)
				{
					continue;
				}
				// A failed gate does not become ready by waiting; the request fails
				// now instead of at the caller's timeout. Content still to come is
				// not judged by the failure of the content it replaces.
				if (!readinessFailure.empty() &&
					entry.m_Request.m_Timing == FrameCaptureTiming::AfterReady &&
					MatchesRequiredContent(entry, state))
				{
					Finish(entry, FrameCaptureRequestStatus::Failed, readinessFailure);
				}
				else if (IsDue(entry, state))
				{
					const FrameCaptureRequest& request = entry.m_Request;
					if (request.m_Source == FrameCaptureSource::Diagnostic && request.m_DiagnosticTap &&
						!GetFrameCaptureDiagnosticTapName(*request.m_DiagnosticTap).empty() &&
						diagnosticTap && diagnosticTap != request.m_DiagnosticTap)
					{
						if (request.m_Sequence)
						{
							Finish(entry, FrameCaptureRequestStatus::Failed,
								"A different diagnostic tap occupies the requested sequence frame.");
						}
						continue;
					}
					Issue(entry, state);
					if (entry.m_Phase == Phase::Issued && request.m_Source == FrameCaptureSource::Diagnostic)
					{
						diagnosticTap = request.m_DiagnosticTap;
					}
				}
			}
		}
		m_LastFrameState = std::move(state);
	}

	std::optional<FrameCaptureViewChange> FrameCaptureCoordinator::GetPendingViewChange()
		const noexcept
	{
		if (m_IsShuttingDown || !m_LastFrameState)
		{
			return std::nullopt;
		}
		// Views apply in submission order, so earlier requests keep the camera
		// they were submitted against until they are issued.
		const auto head = std::ranges::find(m_Entries, Phase::Waiting, &Entry::m_Phase);
		if (head == m_Entries.end() || head->m_Request.m_ReferenceViewId.empty() ||
			head->m_ViewApplied || !MatchesRequiredContent(*head, *m_LastFrameState))
		{
			return std::nullopt;
		}
		// An after-ready view waits for loaded content, which registers its views.
		if (head->m_Request.m_Timing == FrameCaptureTiming::AfterReady &&
			!m_LastFrameState->m_Readiness.IsReady())
		{
			return std::nullopt;
		}
		return FrameCaptureViewChange{
			.m_RequestId = head->m_Id,
			.m_ReferenceViewId = head->m_Request.m_ReferenceViewId,
		};
	}

	bool FrameCaptureCoordinator::ShouldRestartTemporalHistory() const noexcept
	{
		if (m_IsShuttingDown || !m_LastFrameState || m_HistoryReady ||
			!m_LastFrameState->m_Readiness.IsReady())
		{
			return false;
		}
		return std::ranges::any_of(m_Entries, [this](const Entry& entry)
			{
				// A view restored for this frame is already a camera cut.
				const bool viewSettling = entry.m_Request.m_ReferenceViewId.empty() ||
					entry.m_ViewCameraResetSerial.has_value();
				return entry.m_Phase == Phase::Waiting &&
					entry.m_Request.m_Timing == FrameCaptureTiming::AfterReady &&
					viewSettling && MatchesRequiredContent(entry, *m_LastFrameState);
			});
	}

	bool FrameCaptureCoordinator::ShouldHoldTime() const noexcept
	{
		if (m_IsShuttingDown || m_HistoryReady)
		{
			return false;
		}
		return std::ranges::any_of(m_Entries, [](const Entry& entry)
			{
				return entry.m_Phase == Phase::Waiting &&
					entry.m_Request.m_Timing == FrameCaptureTiming::AfterReady;
			});
	}

	void FrameCaptureCoordinator::OnReferenceViewApplied(uint64_t requestId, bool restored) noexcept
	{
		const auto entry = std::ranges::find_if(m_Entries, [&](const Entry& candidate)
			{
				return candidate.m_Id == requestId && candidate.m_Phase == Phase::Waiting;
			});
		if (entry == m_Entries.end())
		{
			return;
		}
		if (restored)
		{
			entry->m_ViewApplied = true;
			entry->m_ViewCameraResetSerial.reset();
			return;
		}

		std::string available;
		std::string content;
		if (m_LastFrameState)
		{
			for (const std::string& id : m_LastFrameState->m_ReferenceViewIds)
			{
				available += available.empty() ? id : ", " + id;
			}
			content = !m_LastFrameState->m_LabId.empty() ? m_LastFrameState->m_LabId
				: m_LastFrameState->m_DemoId;
		}
		Finish(*entry, FrameCaptureRequestStatus::Failed,
			std::format("Reference view '{}' is not registered by '{}' (available: {}).",
				entry->m_Request.m_ReferenceViewId, content,
				available.empty() ? "none" : available));
	}

	void FrameCaptureCoordinator::OnFrameSubmitted() noexcept
	{
		if (m_HasOpenFrame && m_HistoryReady &&
			m_SettledFrames < std::numeric_limits<uint32_t>::max())
		{
			++m_SettledFrames;
		}
		m_HasOpenFrame = false;
	}

	void FrameCaptureCoordinator::Update() noexcept
	{
		if (m_Capture)
		{
			std::vector<FrameCaptureResult> results;
			m_Capture->ConsumeResults(results);
			for (FrameCaptureResult& result : results)
			{
				HandleCaptureResult(std::move(result));
			}
		}
		CollectWrittenJobs();
	}

	void FrameCaptureCoordinator::PrepareForShutdown() noexcept
	{
		m_IsShuttingDown = true;
		for (Entry& entry : m_Entries)
		{
			if (entry.m_Phase == Phase::Waiting)
			{
				Finish(entry, FrameCaptureRequestStatus::Cancelled,
					"The runtime shut down before the capture was due.");
			}
		}
		Update();
	}

	void FrameCaptureCoordinator::FinalizeAfterRenderHost() noexcept
	{
		m_IsShuttingDown = true;
		Update();
		FinishWriting();
		for (Entry& entry : m_Entries)
		{
			if (entry.m_Phase == Phase::Waiting)
			{
				Finish(entry, FrameCaptureRequestStatus::Cancelled,
					"The runtime shut down before the capture was due.");
			}
			else if (entry.m_Phase == Phase::Issued)
			{
				Finish(entry, FrameCaptureRequestStatus::Failed,
					"The render host finalized without a capture result.");
			}
		}
		m_Capture = nullptr;
	}

	bool FrameCaptureCoordinator::MatchesRequiredContent(
		const Entry& entry, const FrameCaptureFrameState& state) noexcept
	{
		const std::string& required = entry.m_Request.m_RequiredContentId;
		return required.empty() || required == state.m_DemoId || required == state.m_LabId;
	}

	bool FrameCaptureCoordinator::IsDue(
		const Entry& entry, const FrameCaptureFrameState& state) const noexcept
	{
		const FrameCaptureRequest& request = entry.m_Request;
		if (!request.m_ReferenceViewId.empty() && !entry.m_ViewApplied)
		{
			return false;
		}
		if (request.m_Timing == FrameCaptureTiming::NextFrame)
		{
			return true;
		}
		return m_HistoryReady && MatchesRequiredContent(entry, state) &&
			m_SettledFrames >= request.m_SettleFrames;
	}

	bool FrameCaptureCoordinator::PrepareResourceExports(uint64_t frameSerial, RenderGraph& graph,
		ResourceExportFrameContext context) noexcept
	{
		if (!m_Capture)
		{
			return true;
		}
		const bool waitingSequenceExport = m_LastFrameState && std::ranges::any_of(m_Entries, [&](const Entry& entry)
			{ return entry.m_Phase == Phase::Waiting && entry.m_Request.m_Sequence &&
			entry.m_Request.m_Source == FrameCaptureSource::ResourceExport && IsDue(entry, *m_LastFrameState); });
		if (waitingSequenceExport)
		{
			m_Capture->DeferFrameCaptures(frameSerial);
		}
		const bool prepared = !waitingSequenceExport &&
			m_Capture->PrepareResourceExports(frameSerial, graph, context) != FrameCaptureAvailability::Busy;
		if (!prepared)
		{
			m_HasOpenFrame = false;
			Update();
		}
		return prepared;
	}

	void FrameCaptureCoordinator::Issue(Entry& entry, const FrameCaptureFrameState& state) noexcept
	{
		if (!m_Capture)
		{
			Finish(entry, FrameCaptureRequestStatus::Failed, "Frame capture is unavailable.");
			return;
		}
		if (state.m_FrameSerial == 0)
		{
			Finish(entry, FrameCaptureRequestStatus::Failed, "Frame capture requires a live render frame serial.");
			return;
		}
		const FrameCaptureRequest& request = entry.m_Request;
		if (request.m_Source == FrameCaptureSource::Diagnostic && (!request.m_DiagnosticTap ||
			GetFrameCaptureDiagnosticTapName(*request.m_DiagnosticTap).empty()))
		{
			Finish(entry, FrameCaptureRequestStatus::Failed, "A diagnostic capture requires a supported diagnostic tap.");
			return;
		}
		const FrameCaptureAvailability availability = GetCaptureAvailability(entry.m_Request.m_Source, state);
		if (availability != FrameCaptureAvailability::Available)
		{
			if (request.m_Source == FrameCaptureSource::ResourceExport && availability == FrameCaptureAvailability::Busy)
			{
				return;
			}
			const char* failure = "Capture capacity is unavailable before GPU submission.";
			if (availability == FrameCaptureAvailability::TooLarge)
			{
				failure = "The capture target exceeds the payload byte budget.";
			}
			else if (availability == FrameCaptureAvailability::Unsupported)
			{
				failure = "The display target has no capture footprint.";
			}
			Finish(entry, FrameCaptureRequestStatus::Failed, failure);
			return;
		}

		FrameCaptureMetadata& metadata = entry.m_Metadata;
		metadata.m_RequestId = entry.m_Id;
		metadata.m_Label = request.m_Label;
		metadata.m_Note = request.m_Note;
		metadata.m_Source = request.m_Source;
		metadata.m_Timing = request.m_Timing;
		metadata.m_SettleFrames =
			request.m_Timing == FrameCaptureTiming::AfterReady ? request.m_SettleFrames : 0;
		metadata.m_SettledFrames = m_SettledFrames;
		metadata.m_Backend = state.m_Backend;
		metadata.m_DemoId = state.m_DemoId;
		metadata.m_LabId = state.m_LabId;
		metadata.m_FrameSerial = state.m_FrameSerial;
		metadata.m_FrameIndex = state.m_FrameIndex;
		metadata.m_Camera = state.m_Camera;
		metadata.m_Camera.m_ReferenceViewId = request.m_ReferenceViewId;
		metadata.m_FixedDeltaTime = state.m_FixedDeltaTime;
		metadata.m_TotalTime = state.m_TotalTime;
		metadata.m_DevelopmentTools = state.m_DevelopmentTools;
		metadata.m_Readiness = state.m_Readiness;
		metadata.m_Temporal = state.m_Temporal;
		metadata.m_Sequence = request.m_Sequence;
		metadata.m_DiagnosticTap = request.m_Source == FrameCaptureSource::Diagnostic &&
			request.m_DiagnosticTap
			? std::string(GetFrameCaptureDiagnosticTapName(*request.m_DiagnosticTap))
			: std::string();
		metadata.m_CapturedAtUtc = std::format("{:%FT%TZ}",
			std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()));

		if (request.m_Source == FrameCaptureSource::ResourceExport)
		{
			entry.m_CaptureRequestId = m_Capture->RequestResourceExport(request.m_ResourceTargets,
				state.m_FrameSerial, request.m_Sequence.has_value(), request.m_IncludeGraph);
		}
		else if (request.m_Source == FrameCaptureSource::Diagnostic)
		{
			entry.m_CaptureRequestId = m_Capture->RequestDiagnosticCapture(
				*request.m_DiagnosticTap, state.m_FrameSerial);
		}
		else
		{
			entry.m_CaptureRequestId = m_Capture->RequestCapture(request.m_Source, state.m_FrameSerial);
		}
		if (entry.m_CaptureRequestId == 0)
		{
			Finish(entry, FrameCaptureRequestStatus::Failed, "Frame capture request capacity is exhausted.");
			return;
		}
		entry.m_Phase = Phase::Issued;
	}

	void FrameCaptureCoordinator::HandleCaptureResult(FrameCaptureResult result) noexcept
	{
		const auto entry = std::ranges::find_if(m_Entries, [&](const Entry& candidate)
			{
				return candidate.m_Phase == Phase::Issued &&
					candidate.m_CaptureRequestId == result.m_RequestId;
			});
		if (entry == m_Entries.end())
		{
			GGLAB_LOG_WARN("Frame capture result {} has no coordinator request.",
				result.m_RequestId);
			return;
		}

		const bool graph = entry->m_Request.m_Source == FrameCaptureSource::RenderGraph;
		if (result.m_Status == FrameCaptureStatus::Busy)
		{
			if (result.m_FrameSerial != entry->m_Metadata.m_FrameSerial || result.m_Source != entry->m_Request.m_Source)
			{
				Finish(*entry, FrameCaptureRequestStatus::Failed, "The deferred capture result does not match its requested frame or source.");
				return;
			}
			entry->m_Phase = Phase::Waiting;
			entry->m_CaptureRequestId = 0;
			entry->m_Metadata = {};
			return;
		}
		const bool resources = entry->m_Request.m_Source == FrameCaptureSource::ResourceExport;
		entry->m_ResourceFailure = result.m_ResourceFailure;
		if (result.m_Status != FrameCaptureStatus::Completed ||
			(resources ? (!result.m_Resources || result.m_Resources->m_Arrays.empty() ||
				(entry->m_Request.m_IncludeGraph && !result.m_Graph)) : graph ? !result.m_Graph : !result.m_Image))
		{
			Finish(*entry, FrameCaptureRequestStatus::Failed,
				result.m_Failure.empty() ? std::string("The frame capture failed.")
				: std::move(result.m_Failure));
			return;
		}
		const std::optional<PostProcessDebugTap> expectedTap =
			entry->m_Request.m_Source == FrameCaptureSource::Diagnostic
			? entry->m_Request.m_DiagnosticTap : std::nullopt;
		if (result.m_FrameSerial != entry->m_Metadata.m_FrameSerial ||
			result.m_Source != entry->m_Request.m_Source || result.m_DiagnosticTap != expectedTap)
		{
			Finish(*entry, FrameCaptureRequestStatus::Failed,
				"The capture result does not match its requested render frame, source or diagnostic tap.");
			return;
		}
		StartEncoding(*entry, result);
	}

	void FrameCaptureCoordinator::StartEncoding(
		Entry& entry, const FrameCaptureResult& result) noexcept
	{
		const std::filesystem::path directory = entry.m_Request.m_OutputDirectory.empty()
			? m_DefaultOutputDirectory
			: entry.m_Request.m_OutputDirectory;
		if (directory.empty())
		{
			Finish(entry, FrameCaptureRequestStatus::Failed,
				"No capture output directory is configured.");
			return;
		}

		FrameCaptureMetadata& metadata = entry.m_Metadata;
		metadata.m_GraphExportActive = result.m_GraphExportActive || bool(result.m_Graph);
		metadata.m_ResourceExportActive = result.m_ResourceExportActive;
		if (result.m_Image)
		{
			const FrameCaptureImage& image = *result.m_Image;
			metadata.m_Width = image.m_Width;
			metadata.m_Height = image.m_Height;
			metadata.m_DisplayFormat = GetRHIFormatInfo(image.m_Format).m_Name;
		}

		const std::string_view subject = !entry.m_Request.m_Label.empty()
			? std::string_view(entry.m_Request.m_Label)
			: !metadata.m_LabId.empty() ? std::string_view(metadata.m_LabId)
			: std::string_view(metadata.m_DemoId);
		const std::string timestamp = metadata.m_CapturedAtUtc.size() >= 19
			? std::format("{}{}{}-{}{}{}", metadata.m_CapturedAtUtc.substr(0, 4),
				metadata.m_CapturedAtUtc.substr(5, 2), metadata.m_CapturedAtUtc.substr(8, 2),
				metadata.m_CapturedAtUtc.substr(11, 2), metadata.m_CapturedAtUtc.substr(14, 2),
				metadata.m_CapturedAtUtc.substr(17, 2))
			: std::string("unknown-time");
		const std::string view = metadata.m_Camera.m_ReferenceViewId.empty()
			? std::string{} : "-" + SanitizeFileStemComponent(metadata.m_Camera.m_ReferenceViewId);
		const std::string baseStem = std::format("{}{}-{}-{}-{}-r{}",
			SanitizeFileStemComponent(subject), view,
			GetFrameCaptureSourceName(metadata.m_Source),
			SanitizeFileStemComponent(metadata.m_Backend), timestamp, entry.m_Id);

		auto job = std::make_shared<EncodeJob>();
		job->m_SubmittedAt = std::chrono::steady_clock::now();
		job->m_Image = result.m_Image;
		job->m_Graph = result.m_Graph;
		job->m_Resources = result.m_Resources;
		job->m_ResourceExportActive = result.m_ResourceExportActive;
		job->m_GraphSummary = entry.m_Request.m_GraphSummary;
		job->m_SubmissionFenceIndex = result.m_SubmissionFenceIndex;
		job->m_SubmissionFenceGeneration = result.m_SubmissionFenceGeneration;
		job->m_SubmissionFenceValue = result.m_SubmissionFenceValue;
		job->m_AdapterCompatibilityIdentity = result.m_AdapterCompatibilityIdentity;
		job->m_Directory = directory;
		job->m_BaseStem = result.m_Resources
			? std::format("{}.{}.resource-export", SanitizeFileStemComponent(subject), metadata.m_FrameSerial) : result.m_Graph
			? std::format("{}.{}.graph{}", SanitizeFileStemComponent(subject), metadata.m_FrameSerial,
				entry.m_Request.m_GraphSummary ? "-summary" : "") : baseStem;
		job->m_TemporaryStem = std::format("{}.{}.{}", job->m_BaseStem, m_TemporaryTag, entry.m_Id);
		job->m_Metadata = metadata;
		entry.m_Job = job;
		entry.m_Phase = Phase::Encoding;

		if (m_WriteOnCallingThread)
		{
			RunEncodeJob(*job, m_ImageEncoder);
			return;
		}
		{
			std::scoped_lock lock(m_Writer->m_Mutex);
			// Slots were reserved before capture. All nine may enter the queue before
			// the writer starts; once it claims one, at most eight remain pending.
			if (m_Writer->m_Queue.size() >= MaxInFlightCaptures)
			{
				Finish(entry, FrameCaptureRequestStatus::Failed, "Capture writer queue is full.");
				return;
			}
			m_Writer->m_Queue.push_back(std::move(job));
		}
		m_Writer->m_Condition.notify_one();
	}

	bool FrameCaptureCoordinator::TryFinishWritten(Entry& entry) noexcept
	{
		EncodeJob& job = *entry.m_Job;
		std::unique_lock lock(job.m_Mutex);
		if (!job.m_IsDone)
		{
			return false;
		}
		std::string failure = std::move(job.m_Failure);
		// Decided before the failure moves into Finish: argument evaluation order
		// is unspecified, so testing it in the same call could see the moved-from text.
		const FrameCaptureRequestStatus status = failure.empty()
			? FrameCaptureRequestStatus::Completed
			: FrameCaptureRequestStatus::Failed;
		if (status == FrameCaptureRequestStatus::Completed)
		{
			entry.m_Metadata.m_ImageFile = job.m_Metadata.m_ImageFile;
		}
		lock.unlock();
		Finish(entry, status, std::move(failure));
		return true;
	}

	void FrameCaptureCoordinator::CollectWrittenJobs() noexcept
	{
		for (Entry& entry : m_Entries)
		{
			if (entry.m_Phase != Phase::Encoding || TryFinishWritten(entry))
			{
				continue;
			}
			EncodeJob& job = *entry.m_Job;
			if (!job.m_StallReported &&
				std::chrono::steady_clock::now() - job.m_SubmittedAt >= EncodeStallReportTime)
			{
				job.m_StallReported = true;
				GGLAB_LOG_WARN_ALWAYS(
					"Frame capture {} has not finished encoding after {} seconds (stage: {}).",
					entry.m_Id, EncodeStallReportTime.count(), GetEncodeStageName(job.m_Stage));
			}
		}
	}

	void FrameCaptureCoordinator::FinishWriting() noexcept
	{
		// One deadline bounds the whole wait, however many captures are unfinished.
		const auto deadline = std::chrono::steady_clock::now() + m_ShutdownWriteTimeout;
		for (Entry& entry : m_Entries)
		{
			if (entry.m_Phase != Phase::Encoding)
			{
				continue;
			}
			EncodeJob& job = *entry.m_Job;
			{
				std::unique_lock lock(job.m_Mutex);
				job.m_Condition.wait_until(lock, deadline, [&job]() { return job.m_IsDone; });
			}
			if (TryFinishWritten(entry))
			{
				continue;
			}

			const EncodeStage stage = job.m_Stage;
			PublicationClaim expected = PublicationClaim::Open;
			if (job.m_Claim.compare_exchange_strong(expected, PublicationClaim::Abandoned))
			{
				m_HasAbandonedJobs = true;
				Finish(entry, FrameCaptureRequestStatus::Failed, std::format(
					"Writing did not finish within {} ms of shutdown (stage: {}); the capture "
					"was abandoned and its files are not published.",
					m_ShutdownWriteTimeout.count(), GetEncodeStageName(stage)));
			}
			else if (!TryFinishWritten(entry))
			{
				// Publication started before the timeout. Its outcome is indeterminate:
				// files may already exist or appear after this failure is reported.
				m_HasAbandonedJobs = true;
				Finish(entry, FrameCaptureRequestStatus::Failed, std::format(
					"Publishing did not finish within {} ms of shutdown; capture publication "
					"is indeterminate and files may already exist or appear later.",
					m_ShutdownWriteTimeout.count()));
			}
		}
	}

	void FrameCaptureCoordinator::StopWriter() noexcept
	{
		if (!m_WriterThread.joinable())
		{
			return;
		}
		{
			std::scoped_lock lock(m_Writer->m_Mutex);
			m_Writer->m_Stopping = true;
		}
		m_Writer->m_Condition.notify_all();
		// File I/O cannot be interrupted. Every capture unfinished at the timeout
		// already has its failed result, and the writer owns all state it still uses.
		// A blocked writer is left to finish on its own; jobs that already claimed
		// publication may still publish files after shutdown reports their failure.
		if (m_HasAbandonedJobs)
		{
			m_WriterThread.detach();
		}
		else
		{
			m_WriterThread.join();
		}
	}

	void FrameCaptureCoordinator::Finish(
		Entry& entry, FrameCaptureRequestStatus status, std::string failure) noexcept
	{
		FrameCaptureRequestResult result{
			.m_RequestId = entry.m_Id,
			.m_Status = status,
			.m_Failure = std::move(failure),
		};
		if (entry.m_Job)
		{
			if (status == FrameCaptureRequestStatus::Completed)
			{
				result.m_ImagePath = entry.m_Job->m_ImagePath;
				result.m_MetadataPath = entry.m_Job->m_MetadataPath;
				result.m_ResourcePaths = entry.m_Job->m_ResourcePaths;
				result.m_GraphPath = entry.m_Job->m_GraphPath;
			}
		}
		if (entry.m_Phase != Phase::Waiting)
		{
			result.m_Metadata = entry.m_Metadata;
		}
		result.m_ResourceFailure = entry.m_ResourceFailure;
		m_Results.push_back(std::move(result));
		entry.m_Job.reset();
		entry.m_Phase = Phase::Finished;
	}

	void FrameCaptureCoordinator::RemoveFinishedEntries() noexcept
	{
		std::erase_if(m_Entries,
			[](const Entry& entry) { return entry.m_Phase == Phase::Finished; });
	}

	void FrameCaptureCoordinator::RunWriter(
		std::shared_ptr<WriterState> state, FrameCaptureImageEncoder encoder) noexcept
	{
		while (true)
		{
			std::shared_ptr<EncodeJob> job;
			{
				std::unique_lock lock(state->m_Mutex);
				state->m_Condition.wait(lock,
					[&state]() { return state->m_Stopping || !state->m_Queue.empty(); });
				if (state->m_Queue.empty())
				{
					return;
				}
				job = std::move(state->m_Queue.front());
				state->m_Queue.pop_front();
			}
			RunEncodeJob(*job, encoder);
		}
	}

	void FrameCaptureCoordinator::RunEncodeJob(
		EncodeJob& job, const FrameCaptureImageEncoder& encoder) noexcept
	{
		// A job abandoned while it was queued is not written at all.
		std::string failure = job.m_Claim == PublicationClaim::Abandoned
			? std::string("The capture was abandoned at shutdown.")
			: WriteAndPublish(job, encoder);
		// Release Runtime payload capacity on the writer thread before reporting
		// completion. No frame-thread state or RHI resource is touched here.
		job.m_Image.reset();
		job.m_Graph.reset();
		job.m_Resources.reset();
		job.m_Stage = EncodeStage::Done;
		{
			std::scoped_lock lock(job.m_Mutex);
			job.m_Failure = std::move(failure);
			job.m_IsDone = true;
		}
		job.m_Condition.notify_all();
	}

	std::string FrameCaptureCoordinator::WriteAndPublish(
		EncodeJob& job, const FrameCaptureImageEncoder& encoder) noexcept try
	{
		const ResourceExportSubmission submission{ job.m_SubmissionFenceIndex, job.m_SubmissionFenceGeneration,
			job.m_SubmissionFenceValue, job.m_AdapterCompatibilityIdentity };
		if (job.m_Resources)
		{
			job.m_Stage = EncodeStage::SerializingGraph;
			std::vector<std::vector<uint8_t>> headers;
			std::vector<std::filesystem::path> temporary;
			for (size_t i = 0; i < job.m_Resources->m_Arrays.size(); ++i)
			{
				headers.push_back(MakeResourceExportNpyHeader(job.m_Resources->m_Arrays[i]));
				temporary.push_back(job.m_Directory / std::format("{}.{}.npy.partial", job.m_TemporaryStem, i));
			}
			const auto metadataTemporary = job.m_Directory / (job.m_TemporaryStem + ".json.partial");
			const auto graphTemporary = job.m_Directory / (job.m_TemporaryStem + ".graph.json.partial");
			const std::string graphJson = job.m_Graph ? SerializeFrameGraphExport(*job.m_Graph,
				job.m_Metadata, submission, false, true) : std::string{};
			const auto names = [&](std::string_view stem)
				{
					std::vector<std::filesystem::path> result;
					for (size_t i = 0; i < temporary.size(); ++i) result.push_back(job.m_Directory / std::format("{}.{}.npy", stem, i));
					return result;
				};
			// Parse/validate metadata before producing any files, including invalid UTF-8.
			const auto firstNames = names(job.m_BaseStem);
			(void)SerializeResourceExportMetadata(*job.m_Resources, job.m_Metadata, submission, firstNames,
				job.m_Graph ? job.m_Directory / (job.m_BaseStem + ".graph.json") : std::filesystem::path{});
			const auto cleanup = [&]()
				{
					std::error_code error;
					for (const auto& path : temporary) std::filesystem::remove(path, error);
					std::filesystem::remove(metadataTemporary, error);
					std::filesystem::remove(graphTemporary, error);
				};
			job.m_Stage = EncodeStage::WritingGraph;
			for (size_t i = 0; i < temporary.size(); ++i)
			{
				const std::string failure = WriteFileContents(temporary[i], headers[i], job.m_Resources->m_Arrays[i].m_Data);
				if (!failure.empty()) { cleanup(); return failure; }
			}
			if (job.m_Graph)
			{
				const std::string failure = WriteFileContents(graphTemporary,
					{ reinterpret_cast<const uint8_t*>(graphJson.data()), graphJson.size() });
				if (!failure.empty()) { cleanup(); return failure; }
			}
			PublicationClaim expected = PublicationClaim::Open;
			if (!job.m_Claim.compare_exchange_strong(expected, PublicationClaim::Publishing))
			{
				cleanup(); return "The resource export was abandoned at shutdown.";
			}
			job.m_Stage = EncodeStage::Publishing;
			for (uint32_t attempt = 1; attempt <= MaxPublishAttempts; ++attempt)
			{
				const std::string stem = attempt == 1 ? job.m_BaseStem : std::format("{}-{}", job.m_BaseStem, attempt);
				const auto files = names(stem);
				const auto graphPath = job.m_Graph ? job.m_Directory / (stem + ".graph.json") : std::filesystem::path{};
				const auto metadataPath = job.m_Directory / (stem + ".json");
				const std::string json = SerializeResourceExportMetadata(*job.m_Resources, job.m_Metadata, submission, files, graphPath);
				std::vector<std::pair<std::filesystem::path, std::filesystem::path>> published;
				PublishOutcome outcome = PublishOutcome::Published;
				for (size_t i = 0; i < files.size(); ++i)
				{
					outcome = PublishNewFile(temporary[i], files[i]);
					if (outcome != PublishOutcome::Published)
					{
						break;
					}
					published.emplace_back(temporary[i], files[i]);
				}
				if (outcome == PublishOutcome::Published && job.m_Graph)
				{
					outcome = PublishNewFile(graphTemporary, graphPath);
					if (outcome == PublishOutcome::Published)
					{
						published.emplace_back(graphTemporary, graphPath);
					}
				}
				std::string failure;
				if (outcome == PublishOutcome::Published)
				{
					failure = WriteFileContents(metadataTemporary, { reinterpret_cast<const uint8_t*>(json.data()), json.size() });
					outcome = failure.empty() ? PublishNewFile(metadataTemporary, metadataPath) : PublishOutcome::Failed;
				}
				if (outcome == PublishOutcome::Published)
				{
					job.m_MetadataPath = metadataPath;
					job.m_ResourcePaths = files;
					job.m_GraphPath = graphPath;
					cleanup();
					return {};
				}
				// Withdraw only files this attempt created. Restore renamed temporary
				// files on volumes without hard links before trying the next group name.
				for (const auto& [partial, path] : published)
				{
					std::error_code error;
					if (std::filesystem::exists(partial, error))
					{
						std::filesystem::remove(path, error);
					}
					else
					{
						std::filesystem::rename(path, partial, error);
					}
					if (error) { cleanup(); return "Resource export rollback failed; publication is indeterminate."; }
				}
				if (outcome == PublishOutcome::Failed)
				{
					cleanup(); return failure.empty() ? "Publishing the resource export group failed." : failure;
				}
			}
			cleanup();
			return "No unused group file name was found for the resource export.";
		}
		if (job.m_Graph)
		{
			job.m_Stage = EncodeStage::SerializingGraph;
			const std::string json = SerializeFrameGraphExport(*job.m_Graph, job.m_Metadata,
				submission, job.m_GraphSummary, job.m_ResourceExportActive);
			const auto temporary = job.m_Directory / (job.m_TemporaryStem + ".json.partial");
			const auto* bytes = reinterpret_cast<const uint8_t*>(json.data());
			job.m_Stage = EncodeStage::WritingGraph;
			std::string failure = WriteFileContents(temporary, { bytes, json.size() });
			if (!failure.empty()) return failure;
			PublicationClaim expected = PublicationClaim::Open;
			if (!job.m_Claim.compare_exchange_strong(expected, PublicationClaim::Publishing))
			{
				std::error_code error;
				std::filesystem::remove(temporary, error);
				return "The graph export was abandoned at shutdown.";
			}
			job.m_Stage = EncodeStage::Publishing;
			for (uint32_t attempt = 1; attempt <= MaxPublishAttempts; ++attempt)
			{
				const auto path = job.m_Directory / ((attempt == 1 ? job.m_BaseStem
					: std::format("{}-{}", job.m_BaseStem, attempt)) + ".json");
				const PublishOutcome outcome = PublishNewFile(temporary, path);
				if (outcome == PublishOutcome::NameTaken) continue;
				std::error_code error;
				std::filesystem::remove(temporary, error);
				if (outcome == PublishOutcome::Failed)
					return std::format("Publishing '{}' failed.", GetFrameCapturePathText(path));
				job.m_MetadataPath = path;
				return {};
			}
			std::error_code error;
			std::filesystem::remove(temporary, error);
			return "No unused file name was found for the graph export.";
		}
		if (!encoder)
		{
			return "No capture image encoder is configured.";
		}
		job.m_Stage = EncodeStage::Encoding;
		const std::optional<std::vector<uint8_t>> encoded =
			job.m_Image ? encoder(*job.m_Image) : std::nullopt;
		if (!encoded)
		{
			return "The captured image could not be encoded as PNG.";
		}

		const std::filesystem::path temporaryImage =
			job.m_Directory / (job.m_TemporaryStem + ".png.partial");
		const std::filesystem::path temporaryMetadata =
			job.m_Directory / (job.m_TemporaryStem + ".json.partial");
		const auto removeTemporaryFiles = [&]() noexcept
			{
				std::error_code errorCode;
				std::filesystem::remove(temporaryImage, errorCode);
				std::filesystem::remove(temporaryMetadata, errorCode);
			};
		job.m_Stage = EncodeStage::WritingImage;
		std::string failure = WriteFileContents(temporaryImage, *encoded);
		if (!failure.empty())
		{
			return failure;
		}

		// Shutdown may have abandoned the job before publication started. Only
		// that claim guarantees that its files never appear after a failed result.
		PublicationClaim expected = PublicationClaim::Open;
		if (!job.m_Claim.compare_exchange_strong(expected, PublicationClaim::Publishing))
		{
			removeTemporaryFiles();
			return "The capture was abandoned at shutdown.";
		}

		job.m_Stage = EncodeStage::Publishing;
		for (uint32_t attempt = 1; attempt <= MaxPublishAttempts; ++attempt)
		{
			const std::string stem = attempt == 1
				? job.m_BaseStem
				: std::format("{}-{}", job.m_BaseStem, attempt);
			const std::filesystem::path imagePath = job.m_Directory / (stem + ".png");
			const std::filesystem::path metadataPath = job.m_Directory / (stem + ".json");
			const PublishOutcome image = PublishNewFile(temporaryImage, imagePath);
			if (image == PublishOutcome::NameTaken)
			{
				continue;
			}
			if (image == PublishOutcome::Failed)
			{
				removeTemporaryFiles();
				return std::format("Publishing '{}' failed.", GetFrameCapturePathText(imagePath));
			}

			// The sidecar names the image file, so it is written once the name is fixed.
			job.m_Stage = EncodeStage::WritingMetadata;
			job.m_Metadata.m_ImageFile = GetFrameCapturePathText(imagePath.filename());
			const std::string json = SerializeFrameCaptureMetadata(job.m_Metadata);
			const auto* jsonBytes = reinterpret_cast<const uint8_t*>(json.data());
			failure = WriteFileContents(
				temporaryMetadata, std::span<const uint8_t>(jsonBytes, json.size()));
			const PublishOutcome metadata = failure.empty()
				? PublishNewFile(temporaryMetadata, metadataPath)
				: PublishOutcome::Failed;
			if (metadata == PublishOutcome::Published)
			{
				removeTemporaryFiles();
				job.m_ImagePath = imagePath;
				job.m_MetadataPath = metadataPath;
				return {};
			}

			// A capture is complete only with both files. This image was created
			// above, so withdrawing it never touches another writer's file; after a
			// rename fallback it moves back to be published under the next name.
			std::error_code errorCode;
			if (std::filesystem::exists(temporaryImage, errorCode))
			{
				std::filesystem::remove(imagePath, errorCode);
			}
			else
			{
				std::filesystem::rename(imagePath, temporaryImage, errorCode);
			}
			if (metadata == PublishOutcome::Failed)
			{
				removeTemporaryFiles();
				return failure.empty()
					? std::format("Publishing '{}' failed.", GetFrameCapturePathText(metadataPath))
					: failure;
			}
			job.m_Stage = EncodeStage::Publishing;
		}
		removeTemporaryFiles();
		return std::format("No unused file name was found for '{}' after {} attempts.",
			job.m_BaseStem, MaxPublishAttempts);
	}
	catch (const nlohmann::json::exception& error)
	{
		// Invalid UTF-8 or malformed metadata must fail the request, not terminate
		// the noexcept writer. Serialization finishes before any graph file is written.
		return std::format("Serializing the {} export failed: {}", job.m_Resources ? "resource" : "graph", error.what());
	}
	catch (const std::exception& error)
	{
		return std::format("Serializing the resource export failed: {}", error.what());
	}
}
