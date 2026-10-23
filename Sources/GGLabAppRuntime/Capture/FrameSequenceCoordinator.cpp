#include "Capture/FrameSequenceCoordinator.h"

#include "Capture/NonFiniteWatchCoordinator.h"
#include "Parameters/HostViewParameters.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace gglab
{
	const char* GetFrameSequenceCancelVerdictName(FrameSequenceCancelVerdict verdict) noexcept
	{
		switch (verdict)
		{
		case FrameSequenceCancelVerdict::Cancelled:
			return "Cancelled";
		case FrameSequenceCancelVerdict::TooLate:
			return "TooLate";
		case FrameSequenceCancelVerdict::Finished:
			return "Finished";
		case FrameSequenceCancelVerdict::NotFound:
			return "NotFound";
		}
		return "NotFound";
	}

	std::string FrameSequenceCoordinator::SerializeTimingProduct() const
	{
		using Json = nlohmann::ordered_json;
		const auto summary = [](std::span<const double> values)
			{
				const auto result = SummarizeFrameSequenceTiming(values);
				return Json{
					{ "count", result.m_Count },
					{ "meanMs", result.m_Mean },
					{ "medianMs", result.m_Median },
					{ "p90Ms", result.m_P90 },
					{ "minMs", result.m_Min },
					{ "maxMs", result.m_Max } };
			};
		const FrameSequenceGpuTiming& timing = *m_Status->m_GpuTiming;
		const uint64_t requested = uint64_t(m_Status->m_FrameCount) *
			std::max(1u, m_Status->m_ReferenceSamples);
		if (timing.m_Samples.empty() || timing.m_Samples.size() != requested)
		{
			throw std::runtime_error("A timing product requires every submitted sequence sample.");
		}
		Json samples = Json::array();
		uint64_t first = timing.m_Samples.front().m_FrameSerial;
		uint64_t last = first;
		bool profilingChanged = false;
		for (const auto& sample : timing.m_Samples)
		{
			first = std::min(first, sample.m_FrameSerial);
			last = std::max(last, sample.m_FrameSerial);
			profilingChanged |= sample.m_ProfilingStateChanged;
			samples.push_back({
				{ "frameSerial", sample.m_FrameSerial },
				{ "sequenceFrame", sample.m_SequenceFrame },
				{ "referenceSample", sample.m_ReferenceSample },
				{ "firstFrame", sample.m_FirstFrame },
				{ "cameraCut", sample.m_CameraCut },
				{ "conditions", {
					{ "gpuProfiling", true },
					{ "profilingStateChanged", sample.m_ProfilingStateChanged },
					{ "developmentTools", sample.m_DevelopmentTools },
					{ "graph", sample.m_Observation.m_Graph },
					{ "resourceExport", sample.m_Observation.m_Nodes.m_ResourceExport },
					{ "nonFiniteScan", sample.m_Observation.m_Nodes.m_NonFiniteScan },
					{ "productionCounters", sample.m_Observation.m_Nodes.m_ProductionCounters } } } });
		}
		Json scopes = Json::array();
		bool stable = true;
		for (const auto& series : timing.m_Scopes)
		{
			const bool pass = series.m_Kind == GpuProfileScopeKind::RenderGraphPass;
			stable &= !pass || series.m_SampleIndices.size() == timing.m_Samples.size();
			scopes.push_back({
				{ "kind", pass ? "pass" : "other" },
				{ "name", series.m_Name },
				{ "occurrence", pass ? Json(series.m_Occurrence) : Json(nullptr) },
				{ "sampleIndices", series.m_SampleIndices },
				{ "milliseconds", series.m_Milliseconds },
				{ "statistics", summary(series.m_Milliseconds) } });
		}
		const Json environment = Json::parse(m_Capture->GetEnvironmentJson());
		return Json{
			{ "schema", "gglab.gpu-timing" },
			{ "schemaVersion", 1 },
			{ "sequence", {
				{ "id", m_Status->m_SequenceId },
				{ "path", m_Status->m_CameraPathId },
				{ "pathVersion", m_Status->m_CameraPathVersion },
				{ "frameCount", m_Status->m_FrameCount },
				{ "referenceSamples", m_Status->m_ReferenceSamples },
				{ "firstFrameSerial", first },
				{ "lastFrameSerial", last } } },
			{ "context", Json::parse(m_TimingContext) },
			{ "coverage", {
				{ "requestedSamples", requested },
				{ "recordedSamples", timing.m_Samples.size() },
				{ "samples", std::move(samples) } } },
			{ "frame", {
				{ "milliseconds", timing.m_FrameMilliseconds },
				{ "statistics", summary(timing.m_FrameMilliseconds) } } },
			{ "scopes", std::move(scopes) },
			{ "stablePassSet", stable },
			{ "measurement", {
				{ "gpuProfilingRequested", true },
				{ "validation", environment.at("validation") },
				{ "profilingStateChanged", profilingChanged } } },
			{ "watch", m_Status->m_WatchCoverage ?
				Json::parse(SerializeNonFiniteWatchCoverage(*m_Status->m_WatchCoverage)) : Json(nullptr) } }.dump(2);
	}

	std::string FrameSequenceCoordinator::SerializeParameterFramesProduct() const
	{
		using Json = nlohmann::ordered_json;
		const uint64_t requested = uint64_t(m_Status->m_FrameCount) *
			std::max(1u, m_Status->m_ReferenceSamples);
		if (m_ParameterFrames.empty() || m_ParameterFrames.size() != requested)
		{
			throw std::runtime_error("A parameter-frames product requires every submitted sequence sample.");
		}
		Json frames = Json::array();
		for (const auto& sample : m_ParameterFrames)
		{
			frames.push_back({
				{ "sequenceFrame", sample.m_Frame },
				{ "referenceSample", sample.m_ReferenceSample },
				{ "parameters", Json::parse(SerializeParameterFrameState(*sample.m_Parameters)) } });
		}
		return Json{
			{ "schema", "gglab.parameter-frames" },
			{ "schemaVersion", 1 },
			{ "sequence", {
				{ "id", m_Status->m_SequenceId },
				{ "path", m_Status->m_CameraPathId },
				{ "pathVersion", m_Status->m_CameraPathVersion },
				{ "frameCount", m_Status->m_FrameCount },
				{ "referenceSamples", m_Status->m_ReferenceSamples },
				{ "firstFrameSerial", m_ParameterFrames.front().m_Parameters->m_FrameSerial },
				{ "lastFrameSerial", m_ParameterFrames.back().m_Parameters->m_FrameSerial } } },
			{ "frames", std::move(frames) },
			{ "watch", m_Status->m_WatchCoverage ?
				Json::parse(SerializeNonFiniteWatchCoverage(*m_Status->m_WatchCoverage)) : Json(nullptr) } }.dump(2);
	}

	void FrameSequenceCoordinator::CollectArtifactPublication() noexcept
	{
		if (!m_ArtifactWrite || !m_Status || m_Status->IsTerminal())
		{
			return;
		}
		const auto receipt = m_Capture->GetSequenceArtifactReceipt(*m_ArtifactWrite);
		if (!receipt)
		{
			return;
		}
		const auto collect = [&](std::optional<FrameSequenceArtifactPublication>& target, uint32_t index)
			{
				if (!target)
				{
					return;
				}
				auto& publication = *target;
				publication.m_Outcome = receipt->m_Publication;
				publication.m_Pending = false;
				publication.m_BundleId = receipt->m_BundleId;
				publication.m_Manifest = receipt->m_ManifestPath;
				publication.m_Failure = receipt->m_Failure;
				publication.m_Files.clear();
				for (const auto& product : receipt->m_BundleProducts)
				{
					if (product.m_ProductIndex == index)
					{
						publication.m_Files = product.m_Files;
					}
				}
			};
		collect(m_Status->m_TimingPublication, 0);
		collect(m_Status->m_ParameterPublication, m_Request.m_GpuTiming ? 1 : 0);
		if (receipt->m_Publication == CapturePublicationOutcome::Published)
		{
			m_Status->m_State = FrameSequenceState::Completed;
			if (auto* watch = m_Capture->GetNonFiniteWatch())
			{
				watch->ReleaseCoverage(m_WatchCoverageId);
			}
			m_WatchCoverageId = 0;
		}
		else
		{
			Fail(receipt->m_Failure.empty() ? "Required sequence artifacts were not published." : receipt->m_Failure);
		}
	}

	GTAOSettings ApplyFrameSequenceGTAOOverrides(
		const FrameSequenceGTAOOverrides& overrides, GTAOSettings settings) noexcept
	{
		return ApplyGTAOSettingsPatch({ .m_DirectionCount = overrides.m_DirectionCount,
			.m_StepCount = overrides.m_StepCount,
			.m_TemporalAccumulation = overrides.m_TemporalAccumulation,
			.m_TemporalMaxSamples = overrides.m_TemporalMaxSamples }, settings);
	}

	TemporalAASettings ApplyFrameSequenceTemporalAAOverrides(
		const FrameSequenceTemporalAAOverrides& overrides,
		TemporalAASettings settings) noexcept
	{
		return ApplyTemporalAASettingsPatch(overrides, settings);
	}

	FrameSequenceTimingSummary SummarizeFrameSequenceTiming(
		std::span<const double> milliseconds)
	{
		if (milliseconds.empty())
		{
			return {};
		}
		std::vector<double> sorted(milliseconds.begin(), milliseconds.end());
		std::ranges::sort(sorted);
		const auto percentile = [&sorted](double fraction) noexcept
			{
				const size_t count = sorted.size();
				const auto rank = static_cast<size_t>(
					std::ceil(fraction * static_cast<double>(count)));
				return sorted[std::clamp<size_t>(rank, 1, count) - 1];
			};
		return FrameSequenceTimingSummary{
			.m_Count = static_cast<uint32_t>(sorted.size()),
			.m_Mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) /
				static_cast<double>(sorted.size()),
			.m_Median = percentile(0.5),
			.m_P90 = percentile(0.9),
			.m_Min = sorted.front(),
			.m_Max = sorted.back(),
		};
	}

	namespace
	{
		[[nodiscard]] bool MatchesRequiredContent(
			std::string_view requiredContentId, const FrameCaptureFrameState& state) noexcept
		{
			return requiredContentId.empty() || state.m_DemoId == requiredContentId ||
				state.m_LabId == requiredContentId;
		}

		[[nodiscard]] std::string DescribePendingGates(const FrameCaptureReadiness& readiness)
		{
			std::string text;
			for (const FrameCaptureGate& gate : readiness.m_Gates)
			{
				if (gate.m_State == FrameCaptureGateState::Ready)
				{
					continue;
				}
				text += std::format("{}{} ({}): {}", text.empty() ? "" : "; ", gate.m_Name,
					GetFrameCaptureGateStateName(gate.m_State), gate.m_Detail);
			}
			return text;
		}

	}

	const char* GetFrameSequenceStateName(FrameSequenceState state) noexcept
	{
		switch (state)
		{
		case FrameSequenceState::Waiting:
			return "waiting";
		case FrameSequenceState::Running:
			return "running";
		case FrameSequenceState::Finishing:
			return "finishing";
		case FrameSequenceState::Completed:
			return "completed";
		case FrameSequenceState::Failed:
			return "failed";
		case FrameSequenceState::Cancelled:
			return "cancelled";
		}
		return "unknown";
	}

	FrameSequenceCoordinator::FrameSequenceCoordinator(FrameCaptureCoordinator& capture) noexcept :
		m_Capture(&capture)
	{
	}

	uint64_t FrameSequenceCoordinator::Start(
		FrameSequenceRequest request, std::string& outError) noexcept
	{
		if (IsActive())
		{
			outError = std::format("Sequence {} is still {}.", m_Status->m_SequenceId,
				GetFrameSequenceStateName(m_Status->m_State));
			return 0;
		}
		if (!m_CaptureJobs.empty() || (m_ArtifactWrite && !m_ArtifactWrite->GetReceipt()))
		{
			outError = "The previous sequence's capture jobs are still draining.";
			return 0;
		}
		if (request.m_CameraPathId.empty())
		{
			outError = "A sequence requires a camera path id.";
			return 0;
		}
		if (request.m_CaptureSource != FrameCaptureSource::Scene &&
			request.m_CaptureSource != FrameCaptureSource::Composited &&
			request.m_CaptureSource != FrameCaptureSource::Diagnostic)
		{
			outError = "A sequence capture requires an image source or diagnostic tap.";
			return 0;
		}
		if ((request.m_CaptureSource == FrameCaptureSource::Diagnostic) !=
			request.m_DiagnosticTap.has_value())
		{
			outError = "A diagnostic tap is required for, and only valid with, diagnostic captures.";
			return 0;
		}
		if (request.m_ReferenceSamples > 0 && request.m_DiagnosticTap &&
			*request.m_DiagnosticTap != PostProcessDebugTap::GTAOFinalAO)
		{
			outError = "A reference averages the scene color, or GTAO visibility with the "
				"gtao-final-ao tap; it records no other diagnostic tap.";
			return 0;
		}
		if (request.m_ReferenceSamples > MaxTemporalReferenceSamples)
		{
			outError = std::format("A reference uses at most {} samples per frame.",
				MaxTemporalReferenceSamples);
			return 0;
		}
		if (request.m_ReferenceSamples == 0 && request.m_ReferenceTextureLodBias != 0.0f)
		{
			outError = "A reference texture LOD bias requires reference samples.";
			return 0;
		}
		if (request.m_ReferenceSamples > 0 && !request.m_TemporalAAOverrides.IsEmpty())
		{
			outError = "A reference renders without Temporal AA, so it takes no Temporal AA "
				"overrides.";
			return 0;
		}
		if (request.m_ReferenceSamples > 0 && !request.m_GTAOOverrides.IsEmpty())
		{
			outError = "A reference averages GTAO over its own samples, so it takes no GTAO "
				"overrides.";
			return 0;
		}
		if (request.m_Events.size() > FrameSequenceMaxParameterEvents ||
			(!request.m_Events.empty() && request.m_ReferenceSamples != 0))
		{
			outError = "A sequence supports at most 128 events; reference events are not supported yet.";
			return 0;
		}
		std::ranges::sort(request.m_Events, {}, &FrameSequenceParameterEvent::m_Frame);
		ViewRenderSettingsPatch legacy;
		legacy.m_TemporalAA = request.m_TemporalAAOverrides;
		legacy.m_Lighting.m_GTAO = {
			.m_DirectionCount = request.m_GTAOOverrides.m_DirectionCount,
			.m_StepCount = request.m_GTAOOverrides.m_StepCount,
			.m_TemporalAccumulation = request.m_GTAOOverrides.m_TemporalAccumulation,
			.m_TemporalMaxSamples = request.m_GTAOOverrides.m_TemporalMaxSamples,
		};
		size_t eventItems = 0;
		for (size_t i = 0; i < request.m_Events.size(); ++i)
		{
			const auto& event = request.m_Events[i];
			eventItems += event.m_Values.size();
			if (event.m_Values.empty() || event.m_Values.size() > 128 || eventItems > 1024 ||
				(i != 0 && request.m_Events[i - 1].m_Frame == event.m_Frame))
			{
				outError = "Events require distinct frames and 1-128 items each (1024 total).";
				return 0;
			}
			if (event.m_ExpectEffects && (event.m_ExpectEffects->size() > 1 ||
				(!event.m_ExpectEffects->empty() && event.m_ExpectEffects->front() != "lighting")))
			{
				outError = "Event expectEffects supports only [] or [\"lighting\"].";
				return 0;
			}
			for (const auto& value : event.m_Values)
			{
				if (HasHostViewParameterPatch(value.m_Id, legacy))
				{
					outError = "An event parameter overlaps a legacy sequence override.";
					return 0;
				}
			}
		}
		std::ranges::sort(request.m_CaptureFrames);
		const auto duplicates = std::ranges::unique(request.m_CaptureFrames);
		request.m_CaptureFrames.erase(duplicates.begin(), duplicates.end());
		std::ranges::sort(request.m_Exports, {}, &FrameSequenceResourceExport::m_Frame);
		for (size_t i = 0; i < request.m_Exports.size(); ++i)
		{
			if (request.m_Exports[i].m_Targets.empty() || request.m_Exports[i].m_Targets.size() > 8 ||
				(i != 0 && request.m_Exports[i - 1].m_Frame == request.m_Exports[i].m_Frame))
			{
				outError = "A sequence permits one resource export group of one to eight targets per frame.";
				return 0;
			}
		}
		if (request.m_Label.empty())
		{
			request.m_Label = request.m_CameraPathId;
		}

		m_Request = std::move(request);
		m_Status = FrameSequenceStatus{
			.m_SequenceId = m_NextSequenceId++,
			.m_State = FrameSequenceState::Waiting,
			.m_CameraPathId = m_Request.m_CameraPathId,
			.m_ReferenceSamples = m_Request.m_ReferenceSamples,
			.m_GpuTiming = m_Request.m_GpuTiming ?
				std::optional<FrameSequenceGpuTiming>(std::in_place) :
				std::nullopt,
		};
		m_EventContentIdentity = 0;
		for (const auto& event : m_Request.m_Events)
		{
			m_Status->m_Events.push_back({ .m_Frame = event.m_Frame });
		}
		if (m_Request.m_GpuTiming)
		{
			m_Status->m_TimingPublication.emplace();
		}
		if (m_Request.m_ParameterProvenance)
		{
			m_Status->m_ParameterPublication.emplace();
		}
		m_ParameterFrames.clear();
		m_TimingContext.clear();
		m_ArtifactProduct.reset();
		m_ArtifactWrite.reset();

		m_Frame = 0;
		m_Sample = 0;
		m_AppliedPose.reset();
		m_FrameBegun = false;
		m_CapturedFrame.reset();
		m_ExportedFrame.reset();
		m_AdmissionDeferred = false;
		m_LastSettleKey.reset();
		m_BegunRenderFrameSerial = 0;
		m_PendingGpuTimingFrames.clear();
		m_GpuTimingSubmissionCount = 0;
		m_LastTimingSubmissionSerial = 0;
		m_WatchStarted = false;
		return m_Status->m_SequenceId;
	}

	FrameSequenceCancelVerdict FrameSequenceCoordinator::Cancel() noexcept
	{
		CollectArtifactPublication();
		if (!IsActive())
		{
			return m_Status ? FrameSequenceCancelVerdict::Finished : FrameSequenceCancelVerdict::NotFound;
		}
		if (m_ArtifactWrite && !m_ArtifactWrite->TryCancel())
		{
			CollectArtifactPublication();
			return FrameSequenceCancelVerdict::TooLate;
		}
		if (m_Status->m_TimingPublication)
		{
			m_Status->m_TimingPublication->m_Pending = false;
		}
		if (m_Status->m_ParameterPublication)
		{
			m_Status->m_ParameterPublication->m_Pending = false;
		}

		m_Status->m_State = FrameSequenceState::Cancelled;
		CancelCaptureJobs();
		m_PendingGpuTimingFrames.clear();
		m_FrameBegun = false;
		if (auto* watch = m_Capture->GetNonFiniteWatch())
		{
			watch->ReleaseCoverage(m_WatchCoverageId);
		}
		m_WatchCoverageId = 0;
		return FrameSequenceCancelVerdict::Cancelled;
	}

	const FrameSequenceStatus* FrameSequenceCoordinator::GetStatus() const noexcept
	{
		return m_Status ? &*m_Status : nullptr;
	}

	bool FrameSequenceCoordinator::IsActive() const noexcept
	{
		return m_Status && !m_Status->IsTerminal();
	}

	bool FrameSequenceCoordinator::ShouldDeferFrame() const noexcept
	{
		if (!IsActive())
		{
			return false;
		}
		const auto* previous = m_Capture->GetLastFrameState();
		const bool starting = m_Status->m_State == FrameSequenceState::Waiting && previous &&
			previous->m_Readiness.IsReady() && MatchesRequiredContent(m_Request.m_RequiredContentId, *previous);
		if (m_Status->m_State != FrameSequenceState::Running && !starting)
		{
			return false;
		}
		const CapturePlan plan = MakeFrameCapturePlan();
		if ((IsCaptureDue() || GetDueExport()) &&
			m_Capture->CheckJobAdmission(plan).m_Status == CaptureAdmissionStatus::Busy)
		{
			return true;
		}
		// Already accepted jobs survive a late deferral without another admission.
		return !plan.m_RequiredProducts.empty() &&
			m_Capture->GetCaptureAvailability(plan) == FrameCaptureAvailability::Busy;
	}

	bool FrameSequenceCoordinator::ShouldHoldTime() const noexcept
	{
		// A begun frame without matching submission is the same simulation instant
		// on retry, including sample zero and non-reference sequences.
		return m_Status && m_Status->m_State == FrameSequenceState::Running &&
			(m_Sample > 0 || m_FrameBegun || m_AdmissionDeferred);
	}

	bool FrameSequenceCoordinator::WantsGpuTiming() const noexcept
	{
		return IsActive() && m_Request.m_GpuTiming &&
			(m_Status->m_State != FrameSequenceState::Finishing || !m_PendingGpuTimingFrames.empty());
	}

	bool FrameSequenceCoordinator::IsCaptureDue() const noexcept
	{
		const bool lastSample = m_Request.m_ReferenceSamples == 0 ||
			m_Sample + 1 == m_Request.m_ReferenceSamples;
		return lastSample && m_CapturedFrame != m_Frame &&
			std::ranges::binary_search(m_Request.m_CaptureFrames, m_Frame);
	}

	const FrameSequenceResourceExport* FrameSequenceCoordinator::GetDueExport() const noexcept
	{
		if (m_ExportedFrame == m_Frame || (m_Request.m_ReferenceSamples != 0 &&
			m_Sample + 1 != m_Request.m_ReferenceSamples))
		{
			return nullptr;
		}
		const auto found = std::ranges::find(m_Request.m_Exports, m_Frame, &FrameSequenceResourceExport::m_Frame);
		return found == m_Request.m_Exports.end() ? nullptr : &*found;
	}

	CapturePlan FrameSequenceCoordinator::MakeFrameCapturePlan() const noexcept
	{
		CapturePlan plan{
			.m_OutputDirectory = m_Request.m_OutputDirectory,
			.m_Label = std::format("{}-f{:04}", m_Request.m_Label, m_Frame),
			.m_Note = m_Request.m_Note,
			.m_Sequence = FrameCaptureSequenceInfo{
				.m_SequenceId = m_Status->m_SequenceId,
				.m_CameraPathId = m_Request.m_CameraPathId,
				.m_CameraPathVersion = m_Status->m_CameraPathVersion,
				.m_Frame = m_Frame,
				.m_FrameCount = m_Status->m_FrameCount,
				.m_ReferenceSamples = m_Request.m_ReferenceSamples,
				.m_ReferenceTextureLodBias = m_Request.m_ReferenceTextureLodBias,
			},
		};
		if (m_Request.m_ReferenceSamples != 0 && m_Sample + 1 != m_Request.m_ReferenceSamples)
		{
			return plan;
		}
		if (std::ranges::binary_search(m_Request.m_CaptureFrames, m_Frame))
		{
			switch (m_Request.m_CaptureSource)
			{
			case FrameCaptureSource::Scene:
				plan.m_RequiredProducts.emplace_back(ImageCaptureProduct{ CaptureImageSource::Scene });
				break;
			case FrameCaptureSource::Composited:
				plan.m_RequiredProducts.emplace_back(ImageCaptureProduct{ CaptureImageSource::Composited });
				break;
			case FrameCaptureSource::Diagnostic:
				plan.m_RequiredProducts.emplace_back(DiagnosticCaptureProduct{ m_Request.m_DiagnosticTap });
				break;
			default:
				GGLAB_ASSERT_MSG(false, "Sequence admission validates the capture source.");
				break;
			}
		}
		const auto resource = std::ranges::find(m_Request.m_Exports, m_Frame, &FrameSequenceResourceExport::m_Frame);
		if (resource != m_Request.m_Exports.end())
		{
			plan.m_RequiredProducts.emplace_back(ResourceCaptureProduct{ resource->m_Targets });
			if (resource->m_IncludeGraph)
			{
				plan.m_RequiredProducts.emplace_back(GraphCaptureProduct{});
			}
		}
		return plan;
	}

	std::optional<FrameSequencePoseRequest> FrameSequenceCoordinator::PrepareFrame(
		const FrameCaptureFrameState* previousFrame,
		std::span<const CameraPath> cameraPaths, uint64_t contentIdentity,
		const std::function<bool(std::span<FrameSequenceParameterEvent>, std::string&)>& validateEvents) noexcept
	{
		m_AppliedPose.reset();
		m_FrameBegun = false;
		m_BegunRenderFrameSerial = 0;
		if (!m_Status)
		{
			return std::nullopt;
		}

		if (m_Status->m_State == FrameSequenceState::Waiting)
		{
			if (!previousFrame ||
				!MatchesRequiredContent(m_Request.m_RequiredContentId, *previousFrame))
			{
				return std::nullopt;
			}
			if (previousFrame->m_Readiness.HasFailed())
			{
				Fail(std::format("Content readiness failed before the sequence started: {}",
					DescribePendingGates(previousFrame->m_Readiness)));
				return std::nullopt;
			}
			if (!previousFrame->m_Readiness.IsReady())
			{
				return std::nullopt;
			}

			const auto path =
				std::ranges::find(cameraPaths, m_Request.m_CameraPathId, &CameraPath::m_Id);
			if (path == cameraPaths.end())
			{
				std::string available;
				for (const CameraPath& candidate : cameraPaths)
				{
					available += std::format("{}{}", available.empty() ? "" : ", ",
						candidate.m_Id);
				}
				Fail(std::format("Camera path '{}' is not registered by the active content{}{}.",
					m_Request.m_CameraPathId, available.empty() ? "" : "; available: ",
					available));
				return std::nullopt;
			}
			const uint32_t frameCount = GetCameraPathFrameCount(*path);
			if (frameCount == 0)
			{
				Fail(std::format("Camera path '{}' is invalid.", path->m_Id));
				return std::nullopt;
			}
			if (!m_Request.m_CaptureFrames.empty() &&
				m_Request.m_CaptureFrames.back() >= frameCount)
			{
				Fail(std::format("Capture frame {} is outside camera path '{}' with {} frames.",
					m_Request.m_CaptureFrames.back(), path->m_Id, frameCount));
				return std::nullopt;
			}
			if (!m_Request.m_Exports.empty() && m_Request.m_Exports.back().m_Frame >= frameCount)
			{
				Fail("A resource export frame is outside the selected camera path.");
				return std::nullopt;
			}
			if (!m_Request.m_Events.empty())
			{
				std::string error;
				if (m_Request.m_Events.back().m_Frame >= frameCount || !validateEvents ||
					contentIdentity == 0 || !validateEvents(m_Request.m_Events, error))
				{
					Fail(error.empty() ? "Invalid sequence event plan or frame outside camera path." : std::move(error));
					return std::nullopt;
				}
				m_EventContentIdentity = contentIdentity;
			}
			m_Status->m_CameraPathVersion = path->m_Version;
			m_Status->m_FrameCount = frameCount;
			m_Status->m_State = FrameSequenceState::Running;
		}

		if (m_Status->m_State != FrameSequenceState::Running)
		{
			return std::nullopt;
		}
		if (!m_Request.m_Events.empty() && contentIdentity != m_EventContentIdentity)
		{
			Fail("The content binding changed during a parameter event sequence.");
			return std::nullopt;
		}
		return FrameSequencePoseRequest{
			.m_CameraPathId = m_Request.m_CameraPathId,
			.m_Frame = m_Frame,
			.m_ReferenceSample = m_Request.m_ReferenceSamples == 0 ?
				std::nullopt :
				std::optional(TemporalReferenceSample{
					.m_Index = m_Sample,
					.m_Count = m_Request.m_ReferenceSamples,
					.m_TextureLodBias = m_Request.m_ReferenceTextureLodBias,
					.m_Signal = m_Request.m_DiagnosticTap ?
						TemporalReferenceSignal::AmbientOcclusion :
						TemporalReferenceSignal::SceneColor,
				}),
			.m_TemporalAAOverrides = m_Request.m_TemporalAAOverrides,
			.m_GTAOOverrides = m_Request.m_GTAOOverrides,
		};
	}

	const FrameSequenceParameterEvent* FrameSequenceCoordinator::GetDueParameterEvent() const noexcept
	{
		if (!m_Status || m_Status->m_State != FrameSequenceState::Running)
		{
			return nullptr;
		}
		for (size_t i = 0; i < m_Request.m_Events.size(); ++i)
		{
			if (m_Request.m_Events[i].m_Frame == m_Frame && !m_Status->m_Events[i].m_Batch)
			{
				return &m_Request.m_Events[i];
			}
		}
		return nullptr;
	}

	void FrameSequenceCoordinator::OnParameterEventBatch(const ParameterBatchStatus& batch) noexcept
	{
		if (!m_Status)
		{
			return;
		}
		for (auto& event : m_Status->m_Events)
		{
			if ((event.m_Batch && event.m_Batch->m_BatchId == batch.m_BatchId) ||
				(!event.m_Batch && event.m_Frame == m_Frame))
			{
				event.m_Batch = batch;
				const auto index = static_cast<size_t>(&event - m_Status->m_Events.data());
				const auto& expected = m_Request.m_Events[index].m_ExpectEffects;
				if (expected && std::ranges::any_of(batch.m_Items, [&](const auto& item)
					{
						return item.m_Applied && !item.m_Identity.m_Domain.empty() &&
							std::ranges::find(*expected, item.m_Identity.m_Domain) == expected->end();
					}))
				{
					event.m_Failure = "EffectMismatch: the actual receipt domain differs from expectEffects.";
					Fail(event.m_Failure);
					return;
				}
				if (batch.m_State == ParameterBatchState::Failed ||
					std::ranges::any_of(batch.m_Items, [](const auto& item)
					{
						return item.IsTerminal() && item.m_State != ParameterEffectState::Consumed &&
							item.m_State != ParameterEffectState::NoChange;
					}))
				{
					event.m_Failure = batch.m_Failure.empty() ? "An event effect did not reach consumption." : batch.m_Failure;
					Fail(event.m_Failure);
				}
				CompleteIfFinished();
				return;
			}
		}
	}

	void FrameSequenceCoordinator::OnContentBindingChanged(uint64_t contentIdentity) noexcept
	{
		if (m_Status && m_Status->m_State == FrameSequenceState::Running &&
			m_EventContentIdentity && contentIdentity != m_EventContentIdentity)
		{
			Fail("The content binding changed during a parameter event sequence.");
		}
	}

	void FrameSequenceCoordinator::OnParameterEventFailed(std::string failure) noexcept
	{
		Fail(std::move(failure));
	}

	void FrameSequenceCoordinator::OnPoseApplied(
		const std::optional<CameraPathPose>& pose) noexcept
	{
		if (!m_Status || m_Status->m_State != FrameSequenceState::Running)
		{
			return;
		}
		if (!pose)
		{
			Fail(std::format("Frame {} of camera path '{}' could not be applied.", m_Frame,
				m_Request.m_CameraPathId));
			return;
		}
		m_AppliedPose = pose;
	}

	bool FrameSequenceCoordinator::BeginFrame(const FrameCaptureFrameState& state,
		const std::function<bool(const FrameCaptureGate&)>& coversReadinessGate) noexcept
	{
		if (!m_Status || m_Status->m_State != FrameSequenceState::Running || !m_AppliedPose)
		{
			return true;
		}
		if (!std::ranges::all_of(state.m_Readiness.m_Gates, [&](const auto& gate)
			{
				return gate.m_State == FrameCaptureGateState::Ready ||
					(gate.m_State == FrameCaptureGateState::Pending && coversReadinessGate &&
						coversReadinessGate(gate));
			}))
		{
			Fail(std::format("A readiness gate left Ready at sequence frame {}: {}", m_Frame,
				DescribePendingGates(state.m_Readiness)));
			return false;
		}
		if (m_LastSettleKey &&
			!state.m_SettleKey.Continues(*m_LastSettleKey, m_AppliedPose->m_Cut))
		{
			const auto& before = m_LastSettleKey->m_Temporal;
			const auto& after = state.m_SettleKey.m_Temporal;
			Fail(std::format("Temporal continuity changed at sequence frame {} outside a camera path cut "
				"(reset {}->{}, session {}->{}, services {}->{}, history={}, identityEqual={}).", m_Frame,
				before.m_Identity.m_ResetIdentity, after.m_Identity.m_ResetIdentity,
				before.m_Identity.m_SessionIdentity, after.m_Identity.m_SessionIdentity,
				static_cast<uint32_t>(before.m_Services), static_cast<uint32_t>(after.m_Services),
				after.m_HasCompatiblePreviousHistory, before.m_Identity == after.m_Identity));
			return false;
		}
		m_LastSettleKey = state.m_SettleKey;
		if (WantsGpuTiming() && state.m_FrameSerial == 0)
		{
			Fail("GPU timing requires a non-zero RenderFrame serial.");
			return false;
		}

		const bool imageDue = IsCaptureDue();
		const bool resourcesDue = GetDueExport() != nullptr;
		if (imageDue || resourcesDue)
		{
			CapturePlan plan = MakeFrameCapturePlan();
			const CaptureAdmission admission = m_Capture->TrySubmit(std::move(plan));
			if (admission.m_Status == CaptureAdmissionStatus::Busy)
			{
				// Admission can change after the early preflight. Preserve this simulation
				// instant on retry and never mark a delivery accepted without an identity.
				m_AdmissionDeferred = true;
				return false;
			}
			if (admission.m_Status != CaptureAdmissionStatus::Accepted)
			{
				Fail(std::format("Capture admission rejected sequence frame {} (reason {}).",
					m_Frame, static_cast<uint32_t>(admission.m_Reason)));
				return false;
			}
			m_CaptureJobs.push_back({ admission.m_JobId, m_Frame,
				static_cast<uint32_t>(imageDue) + static_cast<uint32_t>(resourcesDue) });
			++m_Status->m_AcceptedCaptureJobs;
			m_Status->m_PendingCaptureJobs = static_cast<uint32_t>(m_CaptureJobs.size());
			if (imageDue)
			{
				m_CapturedFrame = m_Frame;
			}
			if (resourcesDue)
			{
				m_ExportedFrame = m_Frame;
			}
		}
		m_AdmissionDeferred = false;
		if (!m_WatchStarted)
		{
			m_WatchStarted = true;
			if (auto* watch = m_Capture->GetNonFiniteWatch())
			{
				m_WatchCoverageId = watch->OpenCoverage();
			}
		}
		m_BegunCameraCut = m_AppliedPose->m_Cut;
		m_BegunDevelopmentTools = state.m_DevelopmentTools;
		if (m_Request.m_GpuTiming && m_TimingContext.empty())
		{
			const auto metadata = nlohmann::ordered_json::parse(SerializeFrameCaptureMetadata({
				.m_Backend = state.m_Backend, .m_DemoId = state.m_DemoId, .m_LabId = state.m_LabId,
				.m_AssetBundle = state.m_AssetBundle, .m_FixedDeltaTime = state.m_FixedDeltaTime,
				.m_Temporal = state.m_Temporal }));
			m_TimingContext = nlohmann::ordered_json{
				{ "backend", metadata.at("backend") },
				{ "content", metadata.at("content") },
				{ "fixedDeltaTime", metadata.at("time").at("fixedDeltaTime") },
				{ "initialTemporal", metadata.at("temporal") } }.dump();
		}
		m_BegunRenderFrameSerial = state.m_FrameSerial;
		m_FrameBegun = true;
		return true;
	}

	void FrameSequenceCoordinator::OnFrameSubmitted(uint64_t renderFrameSerial,
		FrameSequenceObservation observation, std::shared_ptr<const ParameterFrameState> parameters) noexcept
	{
		if (WantsGpuTiming())
		{
			if (renderFrameSerial == 0 || renderFrameSerial <= m_LastTimingSubmissionSerial)
			{
				Fail("GPU timing received an invalid or repeated submitted RenderFrame serial.");
				return;
			}
			m_LastTimingSubmissionSerial = renderFrameSerial;
			++m_GpuTimingSubmissionCount;
		}
		if (!m_FrameBegun || !m_Status || m_Status->m_State != FrameSequenceState::Running)
		{
			return;
		}
		m_FrameBegun = false;
		if (renderFrameSerial != m_BegunRenderFrameSerial)
		{
			Fail("Sequence submission did not match its begun RenderFrame serial.");
			return;
		}
		if (m_Request.m_ParameterProvenance)
		{
			if (!parameters || parameters->m_FrameSerial != renderFrameSerial ||
				(!m_ParameterFrames.empty() && renderFrameSerial <= m_ParameterFrames.back().m_Parameters->m_FrameSerial))
			{
				Fail("Sequence submission requires its matching frozen parameter frame.");
				return;
			}
			m_ParameterFrames.push_back({ m_Frame, m_Sample, std::move(parameters) });
			m_Status->m_ParameterFrameCount = static_cast<uint32_t>(m_ParameterFrames.size());
		}
		for (const auto& event : m_Status->m_Events)
		{
			if (event.m_Frame == m_Frame && (!event.m_Batch ||
				std::ranges::any_of(event.m_Batch->m_Items, [](const auto& item)
				{
					return item.m_DeclaredClass == ParameterEffectClass::Immediate && !item.IsTerminal();
				})))
			{
				Fail("An Immediate event did not reach its terminal receipt in the submitted sequence frame.");
				return;
			}
		}
		if (auto* watch = m_Capture->GetNonFiniteWatch())
		{
			watch->TrackCoverageFrame(m_WatchCoverageId, renderFrameSerial);
		}
		if (WantsGpuTiming())
		{
			m_PendingGpuTimingFrames.push_back({ .m_RenderFrameSerial = renderFrameSerial,
				.m_SubmissionOrdinal = m_GpuTimingSubmissionCount,
				.m_SequenceFrame = m_Frame, .m_ReferenceSample = m_Sample,
				.m_CameraCut = m_BegunCameraCut, .m_DevelopmentTools = m_BegunDevelopmentTools,
				.m_Observation = observation });
		}
		m_AppliedPose.reset();
		if (m_Request.m_ReferenceSamples > 0 && ++m_Sample < m_Request.m_ReferenceSamples)
		{
			return;
		}
		m_Sample = 0;
		++m_Status->m_SubmittedFrames;
		++m_Frame;
		if (m_Frame >= m_Status->m_FrameCount)
		{
			m_Status->m_State = FrameSequenceState::Finishing;
			if (auto* watch = m_Capture->GetNonFiniteWatch())
			{
				watch->CloseCoverage(m_WatchCoverageId);
			}
			UpdateWatchCoverage();
			CompleteIfFinished();
		}
	}

	void FrameSequenceCoordinator::UpdateGpuTiming(
		const GpuProfileFrameSnapshot& profile, bool profilingEnabled) noexcept
	{
		if (!WantsGpuTiming())
		{
			return;
		}
		if (!profilingEnabled)
		{
			Fail("GPU profiling is unavailable or disabled while sequence timing is requested.");
			return;
		}

		const auto pending = std::ranges::find(m_PendingGpuTimingFrames,
			profile.m_RenderFrameSerial, &PendingGpuTimingFrame::m_RenderFrameSerial);
		if (profile.IsValid() && pending != m_PendingGpuTimingFrames.end())
		{
			const auto validDuration = [](double milliseconds) noexcept
				{
					return std::isfinite(milliseconds) && milliseconds >= 0.0;
				};
			if (!validDuration(profile.m_FrameMilliseconds) ||
				!std::ranges::all_of(profile.m_Samples, [&validDuration](const GpuProfileSample& sample)
					{
						return validDuration(sample.m_Milliseconds);
					}))
			{
				Fail(std::format("Invalid GPU timing for RenderFrame {} (sequence frame {}, reference sample {}).",
					pending->m_RenderFrameSerial, pending->m_SequenceFrame, pending->m_ReferenceSample));
				return;
			}
			FrameSequenceGpuTiming& timing = *m_Status->m_GpuTiming;
			for (size_t index = 0; index < profile.m_Samples.size(); ++index)
			{
				const auto& sample = profile.m_Samples[index];
				const bool pass = sample.m_Kind == GpuProfileScopeKind::RenderGraphPass;
				if (sample.m_Name.empty() || (pass ? sample.m_Occurrence == 0 : sample.m_Occurrence != 0) ||
					std::ranges::any_of(std::span(profile.m_Samples).first(index),
						[&sample](const GpuProfileSample& prior) { return prior.GetScope() == sample.GetScope(); }))
				{
					Fail("GPU timing contained an invalid or duplicate scope identity.");
					return;
				}
			}
			const uint32_t sampleIndex = static_cast<uint32_t>(timing.m_Samples.size());
			timing.m_Samples.push_back({ .m_FrameSerial = pending->m_RenderFrameSerial,
				.m_SequenceFrame = pending->m_SequenceFrame, .m_ReferenceSample = pending->m_ReferenceSample,
				.m_FirstFrame = pending->m_SequenceFrame == 0, .m_CameraCut = pending->m_CameraCut,
				.m_DevelopmentTools = pending->m_DevelopmentTools,
				.m_ProfilingStateChanged = profile.m_ProfilingStateChanged,
				.m_Observation = pending->m_Observation });

			timing.m_FrameMilliseconds.push_back(profile.m_FrameMilliseconds);
			for (const GpuProfileSample& sample : profile.m_Samples)
			{
				auto series = std::ranges::find_if(timing.m_Scopes,
					[&sample](const FrameSequenceGpuTimingSeries& candidate)
					{
						return GpuProfileScope{ candidate.m_Name, candidate.m_Kind,
							candidate.m_Occurrence } == sample.GetScope();
					});
				if (series == timing.m_Scopes.end())
				{
					timing.m_Scopes.push_back({ .m_Name = sample.m_Name,
						.m_Kind = sample.m_Kind, .m_Occurrence = sample.m_Occurrence });
					series = std::prev(timing.m_Scopes.end());
				}
				series->m_Milliseconds.push_back(sample.m_Milliseconds);
				series->m_SampleIndices.push_back(sampleIndex);
			}
			// Membership, not the unrelated profiler counter or readback delay,
			// identifies a sample and rejects its subsequent publications.
			m_PendingGpuTimingFrames.erase(pending);
			CompleteIfFinished();
		}
		if (!m_PendingGpuTimingFrames.empty())
		{
			const PendingGpuTimingFrame& oldest = m_PendingGpuTimingFrames.front();
			if (m_GpuTimingSubmissionCount - oldest.m_SubmissionOrdinal >= MaxPendingGpuTimingSubmissions)
			{
				Fail(std::format("Missing GPU timing for RenderFrame {} (sequence frame {}, reference sample {}) "
					"after {} later successful submissions.", oldest.m_RenderFrameSerial,
					oldest.m_SequenceFrame, oldest.m_ReferenceSample, MaxPendingGpuTimingSubmissions));
			}
		}
	}

	void FrameSequenceCoordinator::UpdateCaptureJobs(
		const std::function<std::optional<CaptureJobResult>(uint64_t)>& takeResult) noexcept
	{
		for (size_t index = 0; index < m_CaptureJobs.size();)
		{
			const PendingCaptureJob pending = m_CaptureJobs[index];
			auto result = takeResult(pending.m_JobId);
			if (!result)
			{
				++index;
				continue;
			}
			m_CaptureJobs.erase(m_CaptureJobs.begin() + index);
			++m_Status->m_FinishedCaptureJobs;
			m_Status->m_PendingCaptureJobs = static_cast<uint32_t>(m_CaptureJobs.size());
			if (result->m_Status == CaptureJobStatus::Completed &&
				result->m_Publication == CaptureJobPublication::Published)
			{
				m_Status->m_CompletedCaptures += pending.m_Deliveries;
				continue;
			}
			if (!IsActive())
			{
				continue;
			}
			const auto failure = std::ranges::find_if(result->m_Products,
				[](const auto& product) { return !product.m_Failure.empty(); });
			Fail(std::format("Capture job {} of sequence frame {} {}: {}", pending.m_JobId,
				pending.m_Frame, result->m_Status == CaptureJobStatus::Cancelled ?
					"was cancelled" :
					"failed",
				failure == result->m_Products.end() ? "No complete bundle was published." : failure->m_Failure));
		}
		CompleteIfFinished();
	}

	void FrameSequenceCoordinator::CancelCaptureJobs() noexcept
	{
		for (const PendingCaptureJob& job : m_CaptureJobs)
		{
			// TooLate preserves the writer's real result. Terminal sequences still
			// consume that receipt later without changing their own terminal state.
			GGLAB_UNUSED(m_Capture->CancelJob(job.m_JobId));
		}
		m_AdmissionDeferred = false;
	}

	void FrameSequenceCoordinator::Fail(std::string failure) noexcept
	{
		if (!m_Status || m_Status->IsTerminal())
		{
			return;
		}
		m_Status->m_State = FrameSequenceState::Failed;
		m_Status->m_Failure = std::move(failure);
		CancelCaptureJobs();
		m_PendingGpuTimingFrames.clear();
		m_FrameBegun = false;
		if (auto* watch = m_Capture->GetNonFiniteWatch())
		{
			watch->ReleaseCoverage(m_WatchCoverageId);
		}
		m_WatchCoverageId = 0;
	}

	void FrameSequenceCoordinator::UpdateWatchCoverage() noexcept
	{
		if (!IsActive())
		{
			return;
		}
		if (auto* watch = m_Capture->GetNonFiniteWatch(); watch && m_WatchCoverageId != 0)
		{
			m_Status->m_WatchCoverage = watch->GetCoverage(m_WatchCoverageId);
			if (m_Status->m_WatchCoverage && m_Status->m_WatchCoverage->Fails())
			{
				Fail(m_Status->m_WatchCoverage->m_Failure);
				return;
			}
		}
		CompleteIfFinished();
	}

	void FrameSequenceCoordinator::CompleteIfFinished() noexcept
	{
		CollectArtifactPublication();
		if (m_Status && m_Status->m_State == FrameSequenceState::Finishing &&
			std::ranges::all_of(m_Status->m_Events, [](const auto& event)
			{
				return event.m_Batch && event.m_Batch->IsTerminal();
			}) &&
			m_CaptureJobs.empty() &&
			m_PendingGpuTimingFrames.empty() &&
			(!m_WatchCoverageId || (m_Status->m_WatchCoverage && m_Status->m_WatchCoverage->m_Complete)))
		{
			if (m_Request.m_GpuTiming || m_Request.m_ParameterProvenance)
			{
				if (m_ArtifactWrite)
				{
					return;
				}
				try
				{
					if (!m_ArtifactProduct)
					{
						m_ArtifactProduct = SequenceArtifactBundleInput{
							.m_SequenceId = m_Status->m_SequenceId,
							.m_TimingJson = m_Request.m_GpuTiming ? SerializeTimingProduct() : "",
							.m_ParameterFramesJson = m_Request.m_ParameterProvenance ? SerializeParameterFramesProduct() : "" };
					}
					std::string error;
					m_ArtifactWrite = m_Capture->TryPublishSequenceArtifacts(*m_ArtifactProduct,
						m_Request.m_OutputDirectory, m_Request.m_Label, error);
					if (!error.empty())
					{
						Fail(std::move(error));
						return;
					}
					if (m_ArtifactWrite)
					{
						if (m_Status->m_TimingPublication)
						{
							m_Status->m_TimingPublication->m_Pending = true;
						}
						if (m_Status->m_ParameterPublication)
						{
							m_Status->m_ParameterPublication->m_Pending = true;
						}
						m_ArtifactProduct.reset();
						CollectArtifactPublication();
					}
				}
				catch (const std::exception& error)
				{
					Fail(std::format("Serializing sequence artifacts failed: {}", error.what()));
				}
				return;
			}

			m_Status->m_State = FrameSequenceState::Completed;
			if (auto* watch = m_Capture->GetNonFiniteWatch())
			{
				watch->ReleaseCoverage(m_WatchCoverageId);
			}
			m_WatchCoverageId = 0;
		}
	}
}
