#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/CaptureWriter.h"
#include "Capture/NonFiniteWatchCoordinator.h"

#include "AppRuntimeLog.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <limits>
#include <utility>
#include <variant>

namespace gglab
{
	namespace
	{
		enum class CaptureImageSource : uint8_t
		{
			Scene,
			Composited,
		};

		struct ImageCaptureProduct
		{
			CaptureImageSource m_Source = CaptureImageSource::Scene;
		};

		struct DiagnosticCaptureProduct
		{
			std::optional<PostProcessDebugTap> m_Tap;
		};

		struct GraphCaptureProduct
		{
			bool m_Summary = false;
		};

		struct ResourceCaptureProduct
		{
			std::vector<RGResourceAddress> m_Targets;
			bool m_IncludeGraph = false;
		};

		// Preserve rejection by the Runtime boundary for sources unsupported by
		// the application capture input, rather than interpreting them as images.
		struct UnsupportedCaptureProduct
		{
			FrameCaptureSource m_Source = FrameCaptureSource::Scene;
		};
	}

	struct FrameCaptureCoordinator::CapturePlan
	{
		std::variant<ImageCaptureProduct, DiagnosticCaptureProduct, GraphCaptureProduct,
			ResourceCaptureProduct, UnsupportedCaptureProduct> m_Product;
		FrameCaptureTiming m_Timing = FrameCaptureTiming::NextFrame;
		uint32_t m_SettleFrames = 0;
		std::string m_RequiredContentId;
		std::string m_ReferenceViewId;
		bool m_WaitForCapacity = false;
		std::filesystem::path m_OutputDirectory;
		std::string m_Label;
		std::string m_Note;
		std::optional<FrameCaptureSequenceInfo> m_Sequence;

		[[nodiscard]] FrameCaptureSource GetSource() const noexcept
		{
			if (const auto* image = std::get_if<ImageCaptureProduct>(&m_Product))
			{
				return image->m_Source == CaptureImageSource::Scene ?
					FrameCaptureSource::Scene :
					FrameCaptureSource::Composited;
			}
			if (std::holds_alternative<DiagnosticCaptureProduct>(m_Product))
			{
				return FrameCaptureSource::Diagnostic;
			}
			if (std::holds_alternative<GraphCaptureProduct>(m_Product))
			{
				return FrameCaptureSource::RenderGraph;
			}
			if (std::holds_alternative<ResourceCaptureProduct>(m_Product))
			{
				return FrameCaptureSource::ResourceExport;
			}
			return std::get<UnsupportedCaptureProduct>(m_Product).m_Source;
		}

		[[nodiscard]] std::optional<PostProcessDebugTap> GetDiagnosticTap() const noexcept
		{
			const auto* diagnostic = std::get_if<DiagnosticCaptureProduct>(&m_Product);
			return diagnostic ? diagnostic->m_Tap : std::nullopt;
		}

		[[nodiscard]] bool IsGraphSummary() const noexcept
		{
			const auto* graph = std::get_if<GraphCaptureProduct>(&m_Product);
			return graph && graph->m_Summary;
		}

		[[nodiscard]] bool IncludesGraph() const noexcept
		{
			const auto* resources = std::get_if<ResourceCaptureProduct>(&m_Product);
			return resources && resources->m_IncludeGraph;
		}
	};

	// Host-side association with one Runtime request. GPU owners and their
	// settlement remain in Runtime even after this tracking record is discarded.
	struct FrameCaptureCoordinator::FrameAttempt
	{
		uint64_t m_CaptureRequestId = 0;
		FrameCaptureMetadata m_Metadata{};
		uint64_t m_WatchCoverageId = 0;
		std::optional<FrameCaptureResult> m_PendingResult;
	};

	struct FrameCaptureCoordinator::CaptureJob
	{
		uint64_t m_Id = 0;
		CapturePlan m_Plan{};
		Phase m_Phase = Phase::Waiting;
		// Transient Runtime association for the outputs of one sequence frame.
		uint64_t m_GroupId = 0;
		// An applied view survives capacity deferral; an issued attempt does not.
		// A later camera-reset serial requires restoring the requested view again.
		bool m_ViewApplied = false;
		std::optional<uint64_t> m_ViewCameraResetSerial;
		std::optional<FrameAttempt> m_Attempt;
		// Includes admission failures that occur before an attempt is issued.
		RGObservationFailure m_ResourceFailure = RGObservationFailure::None;
		std::shared_ptr<CaptureWriteJob> m_WriteJob;
		std::optional<CaptureWriteReceipt> m_WriteReceipt;
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
				failure += gate.m_Detail.empty() ?
					gate.m_Name :
					std::format("{} ({})", gate.m_Name, gate.m_Detail);
			}
			if (!failure.empty())
			{
				failure += '.';
			}
			return failure;
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
		m_NonFiniteWatch(createInfo.m_NonFiniteWatch),
		m_DefaultOutputDirectory(createInfo.m_DefaultOutputDirectory),
		m_ShutdownWriteTimeout(createInfo.m_ShutdownWriteTimeout),
		m_Writer(std::make_unique<CaptureWriter>(createInfo.m_ImageEncoder,
			createInfo.m_WriteOnCallingThread, createInfo.m_PublicationFileSystem))
	{
		static_assert(MaxInFlightCaptures == CaptureWriter::MaxInFlightJobs);
	}

	FrameCaptureCoordinator::~FrameCaptureCoordinator()
	{
		FinishWriting();
	}

	FrameCaptureCoordinator::CapturePlan FrameCaptureCoordinator::MakeCapturePlan(
		FrameCaptureRequest request) noexcept
	{
		CapturePlan plan{
			.m_Timing = request.m_Timing,
			.m_SettleFrames = request.m_SettleFrames,
			.m_RequiredContentId = std::move(request.m_RequiredContentId),
			.m_ReferenceViewId = std::move(request.m_ReferenceViewId),
			.m_WaitForCapacity = request.m_WaitForCapacity,
			.m_OutputDirectory = std::move(request.m_OutputDirectory),
			.m_Label = std::move(request.m_Label),
			.m_Note = std::move(request.m_Note),
			.m_Sequence = std::move(request.m_Sequence),
		};
		switch (request.m_Source)
		{
		case FrameCaptureSource::Scene:
			plan.m_Product = ImageCaptureProduct{ CaptureImageSource::Scene };
			break;
		case FrameCaptureSource::Composited:
			plan.m_Product = ImageCaptureProduct{ CaptureImageSource::Composited };
			break;
		case FrameCaptureSource::Diagnostic:
			plan.m_Product = DiagnosticCaptureProduct{ request.m_DiagnosticTap };
			break;
		case FrameCaptureSource::RenderGraph:
			plan.m_Product = GraphCaptureProduct{ request.m_GraphSummary };
			break;
		case FrameCaptureSource::ResourceExport:
			plan.m_Product = ResourceCaptureProduct{
				std::move(request.m_ResourceTargets), request.m_IncludeGraph };
			break;
		default:
			plan.m_Product = UnsupportedCaptureProduct{ request.m_Source };
			break;
		}
		return plan;
	}

	uint64_t FrameCaptureCoordinator::Submit(FrameCaptureRequest request) noexcept
	{
		CaptureJob& entry = m_Jobs.emplace_back();
		entry.m_Id = m_NextRequestId++;
		entry.m_Plan = MakeCapturePlan(std::move(request));
		if (entry.m_Plan.m_Sequence)
		{
			const auto& sequence = *entry.m_Plan.m_Sequence;
			const auto first = std::ranges::find_if(m_Jobs, [&](const CaptureJob& candidate)
				{
					return candidate.m_GroupId != 0 && candidate.m_Plan.m_Sequence &&
						candidate.m_Plan.m_Sequence->m_SequenceId == sequence.m_SequenceId &&
						candidate.m_Plan.m_Sequence->m_Frame == sequence.m_Frame;
				});
			entry.m_GroupId = first == m_Jobs.end() ? entry.m_Id : first->m_GroupId;
		}
		if (m_IsShuttingDown)
		{
			Finish(entry, FrameCaptureRequestStatus::Cancelled,
				"The runtime is shutting down.");
		}
		return entry.m_Id;
	}

	bool FrameCaptureCoordinator::Cancel(uint64_t requestId) noexcept
	{
		for (CaptureJob& entry : m_Jobs)
		{
			if (entry.m_Id == requestId && entry.m_Phase == Phase::Waiting)
			{
				const uint64_t groupId = entry.m_GroupId;
				for (CaptureJob& member : m_Jobs)
				{
					if (member.m_Phase == Phase::Waiting &&
						(member.m_Id == requestId || (groupId != 0 && member.m_GroupId == groupId)))
					{
						Finish(member, FrameCaptureRequestStatus::Cancelled, "Cancelled by request.");
					}
				}
				return true;
			}
		}
		return false;
	}

	void FrameCaptureCoordinator::ConsumeResults(
		std::vector<FrameCaptureRequestResult>& outResults) noexcept
	{
		RemoveFinishedJobs();
		outResults.insert(outResults.end(), std::make_move_iterator(m_Results.begin()),
			std::make_move_iterator(m_Results.end()));
		m_Results.clear();
	}

	uint32_t FrameCaptureCoordinator::GetUnfinishedRequestCount() const noexcept
	{
		return static_cast<uint32_t>(std::ranges::count_if(m_Jobs,
			[](const CaptureJob& entry) { return entry.m_Phase != Phase::Finished; }));
	}

	const FrameCaptureFrameState* FrameCaptureCoordinator::GetLastFrameState() const noexcept
	{
		return m_LastFrameState ? &*m_LastFrameState : nullptr;
	}

	FrameCaptureAvailability FrameCaptureCoordinator::GetCaptureAvailability(FrameCaptureSource source, uint32_t requestCount)
		const noexcept
	{
		if (!m_Capture || m_IsShuttingDown)
		{
			return FrameCaptureAvailability::Finalized;
		}
		if (m_LastFrameState)
		{
			return GetCaptureAvailability(source, *m_LastFrameState, requestCount);
		}
		return GetCaptureAvailability(source, FrameCaptureFrameState{}, requestCount);
	}

	FrameCaptureAvailability FrameCaptureCoordinator::GetCaptureAvailability(FrameCaptureSource source,
		const FrameCaptureFrameState& state, uint32_t requestCount) const noexcept
	{
		if (!m_Capture || m_IsShuttingDown)
		{
			return FrameCaptureAvailability::Finalized;
		}
		// A write slot is reserved before GPU work and stays reserved through encoding.
		// Counting only queued writer jobs misses issued captures still on the GPU.
		const auto reserved = std::ranges::count_if(m_Jobs, [](const CaptureJob& entry)
			{
				return entry.m_Phase == Phase::Issued || entry.m_Phase == Phase::AwaitingWatch || entry.m_Phase == Phase::Encoding;
			});
		if (requestCount > MaxInFlightCaptures)
		{
			return FrameCaptureAvailability::TooLarge;
		}
		if (reserved > MaxInFlightCaptures - requestCount)
		{
			return FrameCaptureAvailability::Busy;
		}
		const auto requests = m_Capture->GetRequestAvailability(requestCount);
		if (requests != FrameCaptureAvailability::Available)
		{
			return requests;
		}
		if (source == FrameCaptureSource::RenderGraph || source == FrameCaptureSource::ResourceExport)
		{
			return source == FrameCaptureSource::ResourceExport ?
				m_Capture->GetResourceExportFrameAvailability() :
				requests;
		}
		if (state.m_FrameSerial == 0)
		{
			return FrameCaptureAvailability::Available;
		}
		const ViewExtent extent = state.m_SettleKey.m_Temporal.m_Identity.m_ColorExtent;
		return m_Capture->GetAvailability(source == FrameCaptureSource::Diagnostic ?
			RHIFormat::R8G8B8A8Unorm :
			state.m_DisplayFormat,
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
		for (CaptureJob& entry : m_Jobs)
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
			// Check every member's share together, then issue the group contiguously
			// on the frame-driving thread before ordinary requests consume any slots.
			IssueSequenceGroups(state, diagnosticTap);
			for (CaptureJob& entry : m_Jobs)
			{
				const bool scheduledGroup = entry.m_GroupId != 0 && std::ranges::any_of(m_Jobs, [&](const CaptureJob& member)
					{
						return member.m_GroupId == entry.m_GroupId && member.m_Plan.GetSource() == FrameCaptureSource::ResourceExport;
					});
				if (entry.m_Phase != Phase::Waiting || scheduledGroup)
				{
					continue;
				}
				// A failed gate does not become ready by waiting; the request fails
				// now instead of at the caller's timeout. Content still to come is
				// not judged by the failure of the content it replaces.
				if (!readinessFailure.empty() &&
					entry.m_Plan.m_Timing == FrameCaptureTiming::AfterReady &&
					MatchesRequiredContent(entry, state))
				{
					Finish(entry, FrameCaptureRequestStatus::Failed, readinessFailure);
				}
				else if (IsDue(entry, state))
				{
					const CapturePlan& request = entry.m_Plan;
					if (request.GetSource() == FrameCaptureSource::Diagnostic && request.GetDiagnosticTap() &&
						!GetFrameCaptureDiagnosticTapName(*request.GetDiagnosticTap()).empty() &&
						diagnosticTap && diagnosticTap != request.GetDiagnosticTap())
					{
						if (request.m_Sequence)
						{
							Finish(entry, FrameCaptureRequestStatus::Failed,
								"A different diagnostic tap occupies the requested sequence frame.");
						}
						continue;
					}
					Issue(entry, state);
					if (entry.m_Phase == Phase::Issued && request.GetSource() == FrameCaptureSource::Diagnostic)
					{
						diagnosticTap = request.GetDiagnosticTap();
					}
				}
			}
		}
		m_LastFrameState = std::move(state);
	}

	void FrameCaptureCoordinator::IssueSequenceGroups(const FrameCaptureFrameState& state,
		std::optional<PostProcessDebugTap>& diagnosticTap) noexcept
	{
		std::vector<uint64_t> visited;
		const std::string readinessFailure = DescribeFailedGates(state.m_Readiness);
		for (CaptureJob& entry : m_Jobs)
		{
			if (entry.m_Phase != Phase::Waiting || entry.m_GroupId == 0 ||
				std::ranges::find(visited, entry.m_GroupId) != visited.end())
			{
				continue;
			}
			visited.push_back(entry.m_GroupId);
			std::vector<CaptureJob*> members;
			for (CaptureJob& member : m_Jobs)
			{
				if (member.m_Phase == Phase::Waiting && member.m_GroupId == entry.m_GroupId)
				{
					members.push_back(&member);
				}
			}
			if (!std::ranges::any_of(members, [](const CaptureJob* member)
				{
					return member->m_Plan.GetSource() == FrameCaptureSource::ResourceExport;
				}))
			{
				continue;
			}
			if (!readinessFailure.empty() && std::ranges::any_of(members, [&](const CaptureJob* member)
				{
					return member->m_Plan.m_Timing == FrameCaptureTiming::AfterReady && MatchesRequiredContent(*member, state);
				}))
			{
				for (CaptureJob* member : members)
				{
					Finish(*member, FrameCaptureRequestStatus::Failed, readinessFailure);
				}
				continue;
			}
			if (!std::ranges::all_of(members, [&](const CaptureJob* member) { return IsDue(*member, state); }))
			{
				continue;
			}
			FrameCaptureAvailability availability = FrameCaptureAvailability::Available;
			std::string failure;
			for (const CaptureJob* member : members)
			{
				const auto& request = member->m_Plan;
				if (request.GetSource() == FrameCaptureSource::Diagnostic && (!request.GetDiagnosticTap() ||
					GetFrameCaptureDiagnosticTapName(*request.GetDiagnosticTap()).empty()))
				{
					failure = "A diagnostic capture requires a supported diagnostic tap.";
					break;
				}
				if (request.GetSource() == FrameCaptureSource::Diagnostic && diagnosticTap && diagnosticTap != request.GetDiagnosticTap())
				{
					failure = "A different diagnostic tap occupies the requested sequence frame.";
					break;
				}
				availability = GetCaptureAvailability(request.GetSource(), state, static_cast<uint32_t>(members.size()));
				if (availability != FrameCaptureAvailability::Available)
				{
					break;
				}
			}
			if (availability == FrameCaptureAvailability::Busy)
			{
				continue;
			}
			if (!failure.empty() || availability != FrameCaptureAvailability::Available)
			{
				if (failure.empty())
				{
					failure = availability == FrameCaptureAvailability::TooLarge ?
						"The sequence capture group exceeds the capture capacity budget." :
						"The sequence capture group is unavailable.";
				}
				for (CaptureJob* member : members)
				{
					if (member->m_Plan.GetSource() == FrameCaptureSource::ResourceExport && availability == FrameCaptureAvailability::TooLarge)
					{
						member->m_ResourceFailure = RGObservationFailure::TooLarge;
					}
					Finish(*member, FrameCaptureRequestStatus::Failed, failure);
				}
				continue;
			}
			for (CaptureJob* member : members)
			{
				Issue(*member, state);
				if (member->m_Phase == Phase::Issued && member->m_Plan.GetSource() == FrameCaptureSource::Diagnostic)
				{
					diagnosticTap = member->m_Plan.GetDiagnosticTap();
				}
				if (member->m_Phase == Phase::Finished)
				{
					for (CaptureJob* remaining : members)
					{
						if (remaining->m_Phase == Phase::Waiting)
						{
							Finish(*remaining, FrameCaptureRequestStatus::Failed, "The same-frame capture group could not be issued.");
						}
					}
					break;
				}
			}
		}
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
		const auto head = std::ranges::find(m_Jobs, Phase::Waiting, &CaptureJob::m_Phase);
		if (head == m_Jobs.end() || head->m_Plan.m_ReferenceViewId.empty() ||
			head->m_ViewApplied || !MatchesRequiredContent(*head, *m_LastFrameState))
		{
			return std::nullopt;
		}
		// An after-ready view waits for loaded content, which registers its views.
		if (head->m_Plan.m_Timing == FrameCaptureTiming::AfterReady &&
			!m_LastFrameState->m_Readiness.IsReady())
		{
			return std::nullopt;
		}
		// A flexible batch keeps its current camera while earlier images retain
		// GPU/readback or writer capacity. The actual frame rechecks before issue.
		if (head->m_Plan.m_WaitForCapacity && !head->m_Plan.m_Sequence &&
			GetCaptureAvailability(head->m_Plan.GetSource(), *m_LastFrameState) == FrameCaptureAvailability::Busy)
		{
			return std::nullopt;
		}
		return FrameCaptureViewChange{
			.m_RequestId = head->m_Id,
			.m_ReferenceViewId = head->m_Plan.m_ReferenceViewId,
		};
	}

	bool FrameCaptureCoordinator::ShouldRestartTemporalHistory() const noexcept
	{
		if (m_IsShuttingDown || !m_LastFrameState || m_HistoryReady ||
			!m_LastFrameState->m_Readiness.IsReady())
		{
			return false;
		}
		return std::ranges::any_of(m_Jobs, [this](const CaptureJob& entry)
			{
				// A view restored for this frame is already a camera cut.
				const bool viewSettling = entry.m_Plan.m_ReferenceViewId.empty() ||
					entry.m_ViewCameraResetSerial.has_value();
				return entry.m_Phase == Phase::Waiting &&
					entry.m_Plan.m_Timing == FrameCaptureTiming::AfterReady &&
					viewSettling && MatchesRequiredContent(entry, *m_LastFrameState);
			});
	}

	bool FrameCaptureCoordinator::ShouldHoldTime() const noexcept
	{
		if (m_IsShuttingDown || m_HistoryReady)
		{
			return false;
		}
		return std::ranges::any_of(m_Jobs, [](const CaptureJob& entry)
			{
				return entry.m_Phase == Phase::Waiting &&
					entry.m_Plan.m_Timing == FrameCaptureTiming::AfterReady;
			});
	}

	void FrameCaptureCoordinator::OnReferenceViewApplied(uint64_t requestId, bool restored) noexcept
	{
		const auto entry = std::ranges::find_if(m_Jobs, [&](const CaptureJob& candidate)
			{
				return candidate.m_Id == requestId && candidate.m_Phase == Phase::Waiting;
			});
		if (entry == m_Jobs.end())
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
			content = !m_LastFrameState->m_LabId.empty() ?
				m_LastFrameState->m_LabId :
				m_LastFrameState->m_DemoId;
		}
		Finish(*entry, FrameCaptureRequestStatus::Failed,
			std::format("Reference view '{}' is not registered by '{}' (available: {}).",
				entry->m_Plan.m_ReferenceViewId, content,
				available.empty() ? "none" : available));
	}

	void FrameCaptureCoordinator::SetFrameSceneInputs(
		uint64_t frameSerial, const FrameCaptureSceneInputs& inputs) noexcept
	{
		for (CaptureJob& entry : m_Jobs)
		{
			if (entry.m_Phase == Phase::Issued && entry.m_Attempt->m_Metadata.m_FrameSerial == frameSerial)
			{
				entry.m_Attempt->m_Metadata.m_SceneInputs = inputs;
			}
		}
	}

	void FrameCaptureCoordinator::OnFrameSubmitted() noexcept
	{
		if (m_NonFiniteWatch && m_LastFrameState)
		{
			for (auto& entry : m_Jobs)
			{
				if (entry.m_Phase == Phase::Issued && entry.m_Attempt->m_WatchCoverageId != 0 &&
					entry.m_Attempt->m_Metadata.m_FrameSerial == m_LastFrameState->m_FrameSerial)
				{
					m_NonFiniteWatch->TrackCoverageFrame(entry.m_Attempt->m_WatchCoverageId, m_LastFrameState->m_FrameSerial);
					m_NonFiniteWatch->CloseCoverage(entry.m_Attempt->m_WatchCoverageId);
				}
			}
		}
		if (m_HasOpenFrame && m_HistoryReady &&
			m_SettledFrames < std::numeric_limits<uint32_t>::max())
		{
			++m_SettledFrames;
		}
		m_HasOpenFrame = false;
	}

	void FrameCaptureCoordinator::Update() noexcept
	{
		if (m_NonFiniteWatch)
		{
			m_NonFiniteWatch->UpdateCoverage();
		}
		if (m_Capture)
		{
			std::vector<FrameCaptureResult> results;
			m_Capture->ConsumeResults(results);
			for (FrameCaptureResult& result : results)
			{
				if (!m_NonFiniteWatch || !m_NonFiniteWatch->OnScanResult(result))
				{
					HandleCaptureResult(std::move(result));
				}
			}
			std::vector<std::shared_ptr<const NonFiniteProductionFrame>> production;
			m_Capture->ConsumeNonFiniteProductionFrames(production);
			if (m_NonFiniteWatch)
			{
				for (const auto& frame : production)
				{
					m_NonFiniteWatch->OnProductionFrame(*frame);
				}
			}
		}
		PollWatchCaptures();
		CollectWrittenJobs();
	}

	void FrameCaptureCoordinator::PrepareForShutdown() noexcept
	{
		m_IsShuttingDown = true;
		for (CaptureJob& entry : m_Jobs)
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
		// Render-host finalization publishes the last fence-completed readbacks.
		// Drain them before closing only the coverage that still lacks evidence.
		Update();
		if (m_NonFiniteWatch)
		{
			m_NonFiniteWatch->FinalizeCoverage();
		}
		Update();
		FinishWriting();
		for (CaptureJob& entry : m_Jobs)
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
		const CaptureJob& entry, const FrameCaptureFrameState& state) noexcept
	{
		const std::string& required = entry.m_Plan.m_RequiredContentId;
		return required.empty() || required == state.m_DemoId || required == state.m_LabId;
	}

	bool FrameCaptureCoordinator::IsDue(
		const CaptureJob& entry, const FrameCaptureFrameState& state) const noexcept
	{
		const CapturePlan& request = entry.m_Plan;
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

	bool FrameCaptureCoordinator::HasFrameResourceExports() const noexcept
	{
		return std::ranges::any_of(m_Jobs, [](const CaptureJob& entry)
			{
				return (entry.m_Phase == Phase::Waiting || entry.m_Phase == Phase::Issued) &&
					entry.m_Plan.GetSource() == FrameCaptureSource::ResourceExport;
			});
	}

	bool FrameCaptureCoordinator::PrepareResourceExports(uint64_t frameSerial, RenderGraph& graph,
		ResourceExportFrameContext context) noexcept
	{
		if (!m_Capture)
		{
			return true;
		}
		const bool strictWatch = m_NonFiniteWatch && m_NonFiniteWatch->GetStatus().m_Enabled &&
			m_NonFiniteWatch->GetStatus().m_Mode == NonFiniteWatchMode::Fail;
		const bool waitingExactExport = m_LastFrameState && std::ranges::any_of(m_Jobs, [&](const CaptureJob& entry)
			{
				return entry.m_Phase == Phase::Waiting && (entry.m_Plan.m_Sequence || strictWatch) &&
					entry.m_Plan.GetSource() == FrameCaptureSource::ResourceExport && IsDue(entry, *m_LastFrameState);
			});
		if (waitingExactExport)
		{
			m_Capture->DeferFrameCaptures(frameSerial);
		}
		const bool prepared = !waitingExactExport &&
			m_Capture->PrepareResourceExports(frameSerial, graph, context) != FrameCaptureAvailability::Busy;
		if (!prepared)
		{
			m_HasOpenFrame = false;
			Update();
		}
		return prepared;
	}

	void FrameCaptureCoordinator::Issue(CaptureJob& entry, const FrameCaptureFrameState& state) noexcept
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
		const CapturePlan& request = entry.m_Plan;
		if (request.GetSource() == FrameCaptureSource::Diagnostic && (!request.GetDiagnosticTap() ||
			GetFrameCaptureDiagnosticTapName(*request.GetDiagnosticTap()).empty()))
		{
			Finish(entry, FrameCaptureRequestStatus::Failed, "A diagnostic capture requires a supported diagnostic tap.");
			return;
		}
		const FrameCaptureAvailability availability = GetCaptureAvailability(entry.m_Plan.GetSource(), state);
		if (availability != FrameCaptureAvailability::Available)
		{
			if (availability == FrameCaptureAvailability::Busy &&
				(request.GetSource() == FrameCaptureSource::ResourceExport ||
					(request.m_WaitForCapacity && !request.m_Sequence)))
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

		GGLAB_ASSERT_MSG(!entry.m_Attempt, "A waiting capture job must not retain a frame attempt.");
		FrameAttempt attempt;
		FrameCaptureMetadata& metadata = attempt.m_Metadata;
		metadata.m_RequestId = entry.m_Id;
		metadata.m_Label = request.m_Label;
		metadata.m_Note = request.m_Note;
		metadata.m_Source = request.GetSource();
		metadata.m_Timing = request.m_Timing;
		metadata.m_SettleFrames =
			request.m_Timing == FrameCaptureTiming::AfterReady ? request.m_SettleFrames : 0;
		metadata.m_SettledFrames = m_SettledFrames;
		metadata.m_Backend = state.m_Backend;
		metadata.m_DemoId = state.m_DemoId;
		metadata.m_LabId = state.m_LabId;
		metadata.m_AssetBundle = state.m_AssetBundle;
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
		metadata.m_DiagnosticTap = request.GetSource() == FrameCaptureSource::Diagnostic &&
			request.GetDiagnosticTap() ?
				std::string(GetFrameCaptureDiagnosticTapName(*request.GetDiagnosticTap())) :
				std::string();
		metadata.m_CapturedAtUtc = std::format("{:%FT%TZ}",
			std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()));

		if (const auto* resources = std::get_if<ResourceCaptureProduct>(&request.m_Product))
		{
			attempt.m_CaptureRequestId = m_Capture->RequestResourceExport(resources->m_Targets,
				state.m_FrameSerial, request.m_Sequence.has_value(), resources->m_IncludeGraph, entry.m_GroupId);
		}
		else if (request.GetSource() == FrameCaptureSource::Diagnostic)
		{
			attempt.m_CaptureRequestId = m_Capture->RequestDiagnosticCapture(
				*request.GetDiagnosticTap(), state.m_FrameSerial, entry.m_GroupId);
		}
		else
		{
			attempt.m_CaptureRequestId = m_Capture->RequestCapture(
				request.GetSource(), state.m_FrameSerial, entry.m_GroupId);
		}
		if (attempt.m_CaptureRequestId == 0)
		{
			Finish(entry, FrameCaptureRequestStatus::Failed, "Frame capture request capacity is exhausted.");
			return;
		}
		attempt.m_WatchCoverageId = m_NonFiniteWatch ? m_NonFiniteWatch->OpenCoverage() : 0;
		entry.m_Attempt = std::move(attempt);
		entry.m_Phase = Phase::Issued;
	}

	void FrameCaptureCoordinator::HandleCaptureResult(FrameCaptureResult result) noexcept
	{
		const auto entry = std::ranges::find_if(m_Jobs, [&](const CaptureJob& candidate)
			{
				return candidate.m_Phase == Phase::Issued &&
					candidate.m_Attempt->m_CaptureRequestId == result.m_RequestId;
			});
		if (entry == m_Jobs.end())
		{
			if (std::erase(m_DiscardedCaptureResults, result.m_RequestId) != 0)
			{
				return;
			}
			GGLAB_LOG_WARN("Frame capture result {} has no coordinator request.",
				result.m_RequestId);
			return;
		}

		FrameAttempt& attempt = *entry->m_Attempt;
		const CapturePlan& plan = entry->m_Plan;
		const FrameCaptureSource source = plan.GetSource();
		const bool graph = source == FrameCaptureSource::RenderGraph;
		if (result.m_Status == FrameCaptureStatus::Busy)
		{
			if (result.m_FrameSerial != attempt.m_Metadata.m_FrameSerial || result.m_Source != source)
			{
				Finish(*entry, FrameCaptureRequestStatus::Failed,
					"The deferred capture result does not match its requested frame or source.");
				return;
			}
			entry->m_Phase = Phase::Waiting;
			if (m_NonFiniteWatch)
			{
				m_NonFiniteWatch->ReleaseCoverage(attempt.m_WatchCoverageId);
			}
			// Busy ends this attempt, not its job. A later frame must start with
			// fresh provenance, Runtime identity and watch coverage together.
			entry->m_Attempt.reset();
			return;
		}
		const bool resources = source == FrameCaptureSource::ResourceExport;
		entry->m_ResourceFailure = result.m_ResourceFailure;
		if (result.m_Status != FrameCaptureStatus::Completed ||
			(resources ?
				(!result.m_Resources || result.m_Resources->m_Arrays.empty() ||
					(plan.IncludesGraph() && !result.m_Graph)) :
				graph ? !result.m_Graph : !result.m_Image))
		{
			Finish(*entry, FrameCaptureRequestStatus::Failed,
				result.m_Failure.empty() ?
					std::string("The frame capture failed.") :
					std::move(result.m_Failure));
			return;
		}
		const std::optional<PostProcessDebugTap> expectedTap =
			source == FrameCaptureSource::Diagnostic ?
				plan.GetDiagnosticTap() :
				std::nullopt;
		if (result.m_FrameSerial != attempt.m_Metadata.m_FrameSerial ||
			result.m_Source != source || result.m_DiagnosticTap != expectedTap)
		{
			Finish(*entry, FrameCaptureRequestStatus::Failed,
				"The capture result does not match its requested render frame, source or diagnostic tap.");
			return;
		}
		attempt.m_PendingResult = std::move(result);
		entry->m_Phase = Phase::AwaitingWatch;
	}

	void FrameCaptureCoordinator::PollWatchCaptures() noexcept
	{
		const std::string failure = m_NonFiniteWatch ? m_NonFiniteWatch->GetFailModeFailure() : std::string{};
		for (auto& entry : m_Jobs)
		{
			if (entry.m_Phase == Phase::Waiting &&
				entry.m_Plan.m_Timing == FrameCaptureTiming::AfterReady && !failure.empty())
			{
				Finish(entry, FrameCaptureRequestStatus::Failed, failure);
			}
			if (entry.m_Phase == Phase::Issued && m_NonFiniteWatch)
			{
				FrameAttempt& attempt = *entry.m_Attempt;
				const auto coverage = m_NonFiniteWatch->GetCoverage(attempt.m_WatchCoverageId);
				if (coverage && coverage->Fails())
				{
					m_DiscardedCaptureResults.push_back(attempt.m_CaptureRequestId);
					attempt.m_Metadata.m_WatchCoverage = coverage;
					Finish(entry, FrameCaptureRequestStatus::Failed, coverage->m_Failure);
					continue;
				}
			}
			if (entry.m_Phase != Phase::AwaitingWatch)
			{
				continue;
			}
			FrameAttempt& attempt = *entry.m_Attempt;
			attempt.m_Metadata.m_WatchCoverage = m_NonFiniteWatch ?
				m_NonFiniteWatch->GetCoverage(attempt.m_WatchCoverageId) :
				nullptr;
			if (const auto& coverage = attempt.m_Metadata.m_WatchCoverage; coverage)
			{
				if (coverage->Fails())
				{
					Finish(entry, FrameCaptureRequestStatus::Failed, coverage->m_Failure);
					continue;
				}
				if (!coverage->m_Complete)
				{
					continue;
				}
			}
			// Encoding can fail synchronously and finish the job, destroying its
			// attempt. Keep the payload local across that transition.
			FrameCaptureResult result = std::move(*attempt.m_PendingResult);
			attempt.m_PendingResult.reset();
			StartEncoding(entry, result);
		}
	}

	void FrameCaptureCoordinator::StartEncoding(
		CaptureJob& entry, const FrameCaptureResult& result) noexcept
	{
		const std::filesystem::path directory = entry.m_Plan.m_OutputDirectory.empty() ?
			m_DefaultOutputDirectory :
			entry.m_Plan.m_OutputDirectory;
		if (directory.empty())
		{
			Finish(entry, FrameCaptureRequestStatus::Failed,
				"No capture output directory is configured.");
			return;
		}

		FrameCaptureMetadata& metadata = entry.m_Attempt->m_Metadata;
		metadata.m_GraphExportActive = result.m_GraphExportActive || bool(result.m_Graph);
		metadata.m_ResourceExportActive = result.m_ResourceExportActive;
		metadata.m_NonFiniteScanActive = result.m_NonFiniteScanActive;
		if (result.m_Image)
		{
			const FrameCaptureImage& image = *result.m_Image;
			metadata.m_Width = image.m_Width;
			metadata.m_Height = image.m_Height;
			metadata.m_DisplayFormat = GetRHIFormatInfo(image.m_Format).m_Name;
		}

		const std::string_view subject = !entry.m_Plan.m_Label.empty() ?
			std::string_view(entry.m_Plan.m_Label) :
			!metadata.m_LabId.empty() ?
				std::string_view(metadata.m_LabId) :
				std::string_view(metadata.m_DemoId);
		const std::string timestamp = metadata.m_CapturedAtUtc.size() >= 19 ?
			std::format("{}{}{}-{}{}{}", metadata.m_CapturedAtUtc.substr(0, 4),
				metadata.m_CapturedAtUtc.substr(5, 2), metadata.m_CapturedAtUtc.substr(8, 2),
				metadata.m_CapturedAtUtc.substr(11, 2), metadata.m_CapturedAtUtc.substr(14, 2),
				metadata.m_CapturedAtUtc.substr(17, 2)) :
			std::string("unknown-time");
		const std::string view = metadata.m_Camera.m_ReferenceViewId.empty() ?
			std::string{} :
			"-" + SanitizeFileStemComponent(metadata.m_Camera.m_ReferenceViewId);
		const std::string baseStem = std::format("{}{}-{}-{}-{}-r{}",
			SanitizeFileStemComponent(subject), view,
			GetFrameCaptureSourceName(metadata.m_Source),
			SanitizeFileStemComponent(metadata.m_Backend), timestamp, entry.m_Id);

		auto job = std::make_shared<CaptureWriteJob>();
		job->m_Products = {
			.m_Image = result.m_Image,
			.m_Graph = result.m_Graph,
			.m_Resources = result.m_Resources,
			.m_ResourceExportActive = result.m_ResourceExportActive,
			.m_GraphSummary = entry.m_Plan.IsGraphSummary(),
			.m_Submission = { result.m_SubmissionFenceIndex, result.m_SubmissionFenceGeneration,
				result.m_SubmissionFenceValue, result.m_AdapterCompatibilityIdentity },
			.m_Metadata = metadata,
		};
		job->m_Directory = directory;
		job->m_BaseStem = result.m_Resources ?
			std::format("{}.{}.resource-export", SanitizeFileStemComponent(subject), metadata.m_FrameSerial) :
			result.m_Graph ?
				std::format("{}.{}.graph{}", SanitizeFileStemComponent(subject), metadata.m_FrameSerial,
					entry.m_Plan.IsGraphSummary() ? "-summary" : "") :
				baseStem;
		job->m_RequestId = entry.m_Id;
		entry.m_WriteJob = job;
		entry.m_Phase = Phase::Encoding;
		if (!m_Writer->Submit(std::move(job)))
		{
			Finish(entry, FrameCaptureRequestStatus::Failed, "Capture writer queue is full.");
		}
	}

	bool FrameCaptureCoordinator::TryFinishWritten(CaptureJob& entry) noexcept
	{
		entry.m_WriteReceipt = entry.m_WriteJob->GetReceipt();
		if (!entry.m_WriteReceipt)
		{
			return false;
		}
		const FrameCaptureRequestStatus status =
			entry.m_WriteReceipt->m_Publication == CapturePublicationOutcome::Published ?
				FrameCaptureRequestStatus::Completed :
				FrameCaptureRequestStatus::Failed;
		if (status == FrameCaptureRequestStatus::Completed && !entry.m_WriteReceipt->m_ImagePath.empty())
		{
			entry.m_Attempt->m_Metadata.m_ImageFile =
				GetFrameCapturePathText(entry.m_WriteReceipt->m_ImagePath.filename());
		}
		Finish(entry, status, entry.m_WriteReceipt->m_Failure);
		return true;
	}

	void FrameCaptureCoordinator::CollectWrittenJobs() noexcept
	{
		for (CaptureJob& entry : m_Jobs)
		{
			if (entry.m_Phase != Phase::Encoding || TryFinishWritten(entry))
			{
				continue;
			}
			CaptureWriteJob& job = *entry.m_WriteJob;
			if (!job.m_StallReported &&
				std::chrono::steady_clock::now() - job.m_SubmittedAt >= EncodeStallReportTime)
			{
				job.m_StallReported = true;
				GGLAB_LOG_WARN_ALWAYS(
					"Frame capture {} has not finished encoding after {} seconds (stage: {}).",
					entry.m_Id, EncodeStallReportTime.count(),
					GetCaptureWriteStageName(job.m_Publication.m_Stage));
			}
		}
	}

	void FrameCaptureCoordinator::FinishWriting() noexcept
	{
		// One deadline bounds the whole wait, however many captures are unfinished.
		const auto deadline = std::chrono::steady_clock::now() + m_ShutdownWriteTimeout;
		for (CaptureJob& entry : m_Jobs)
		{
			if (entry.m_Phase != Phase::Encoding)
			{
				continue;
			}
			entry.m_WriteJob->WaitUntil(deadline);
			if (TryFinishWritten(entry))
			{
				continue;
			}
			entry.m_WriteReceipt = m_Writer->Abandon(*entry.m_WriteJob, m_ShutdownWriteTimeout);
			if (entry.m_WriteReceipt->m_Publication == CapturePublicationOutcome::Published)
			{
				// Completion may race the timeout snapshot. Consume that receipt normally.
				GGLAB_UNUSED(TryFinishWritten(entry));
			}
			else
			{
				Finish(entry, FrameCaptureRequestStatus::Failed, entry.m_WriteReceipt->m_Failure);
			}
		}
	}

	void FrameCaptureCoordinator::Finish(
		CaptureJob& entry, FrameCaptureRequestStatus status, std::string failure) noexcept
	{
		GGLAB_ASSERT_MSG(entry.m_Phase != Phase::Finished, "A capture job must finish exactly once.");
		FrameCaptureRequestResult result{
			.m_RequestId = entry.m_Id,
			.m_Status = status,
			.m_Failure = std::move(failure),
		};
		if (entry.m_WriteReceipt && status == FrameCaptureRequestStatus::Completed)
		{
			result.m_ImagePath = entry.m_WriteReceipt->m_ImagePath;
			result.m_MetadataPath = entry.m_WriteReceipt->m_MetadataPath;
			result.m_ResourcePaths = entry.m_WriteReceipt->m_ResourcePaths;
			result.m_GraphPath = entry.m_WriteReceipt->m_GraphPath;
		}
		if (entry.m_Phase != Phase::Waiting)
		{
			FrameAttempt& attempt = *entry.m_Attempt;
			if (m_NonFiniteWatch && attempt.m_WatchCoverageId != 0 && !attempt.m_Metadata.m_WatchCoverage)
			{
				attempt.m_Metadata.m_WatchCoverage =
					m_NonFiniteWatch->GetCoverage(attempt.m_WatchCoverageId);
			}
			result.m_Metadata = attempt.m_Metadata;
		}
		result.m_ResourceFailure = entry.m_ResourceFailure;
		m_Results.push_back(std::move(result));
		if (m_NonFiniteWatch && entry.m_Attempt)
		{
			m_NonFiniteWatch->ReleaseCoverage(entry.m_Attempt->m_WatchCoverageId);
		}
		entry.m_Attempt.reset();
		entry.m_WriteJob.reset();
		entry.m_Phase = Phase::Finished;
	}

	void FrameCaptureCoordinator::RemoveFinishedJobs() noexcept
	{
		std::erase_if(m_Jobs,
			[](const CaptureJob& entry) { return entry.m_Phase == Phase::Finished; });
	}
}
