#include "Capture/FrameSequenceCoordinator.h"
#include "Capture/NonFiniteWatchCoordinator.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAOTypes.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <numeric>
#include <string_view>
#include <utility>

namespace gglab
{
	bool FrameSequenceTemporalAAOverrides::IsEmpty() const noexcept
	{
		return !m_Enabled && !m_MaxHistoryFeedback && !m_DepthAbsoluteThreshold &&
			!m_DepthRelativeThreshold && !m_VelocityWeightScale && !m_LuminanceWeightScale &&
			!m_NeighborhoodClampExpansion && !m_HistoryRelaxation && !m_HistoryAccumulation &&
			!m_HistoryRectification &&
			!m_VarianceClipGamma && !m_HistoryFilter && !m_CurrentFilter &&
			!m_MotionSelection && !m_PostTemporalView && !m_ResolutionPreset &&
			!m_TextureLodBiasOffset;
	}

	GTAOSettings ApplyFrameSequenceGTAOOverrides(
		const FrameSequenceGTAOOverrides& overrides, GTAOSettings settings) noexcept
	{
		if (overrides.m_TemporalAccumulation)
		{
			settings.m_TemporalAccumulation = *overrides.m_TemporalAccumulation;
		}
		if (overrides.m_TemporalMaxSamples)
		{
			settings.m_TemporalMaxSamples =
				std::clamp(*overrides.m_TemporalMaxSamples, 1u, GTAOMaxTemporalSamples);
		}
		if (overrides.m_DirectionCount)
		{
			settings.m_DirectionCount =
				std::clamp(*overrides.m_DirectionCount, 1u, GTAOMaxDirectionCount);
		}
		if (overrides.m_StepCount)
		{
			settings.m_StepCount = std::clamp(*overrides.m_StepCount, 1u, GTAOMaxStepCount);
		}
		return settings;
	}

	TemporalAASettings ApplyFrameSequenceTemporalAAOverrides(
		const FrameSequenceTemporalAAOverrides& overrides,
		TemporalAASettings settings) noexcept
	{
		settings.m_Enabled = overrides.m_Enabled.value_or(settings.m_Enabled);
		settings.m_MaxHistoryFeedback =
			overrides.m_MaxHistoryFeedback.value_or(settings.m_MaxHistoryFeedback);
		settings.m_DepthAbsoluteThreshold =
			overrides.m_DepthAbsoluteThreshold.value_or(settings.m_DepthAbsoluteThreshold);
		settings.m_DepthRelativeThreshold =
			overrides.m_DepthRelativeThreshold.value_or(settings.m_DepthRelativeThreshold);
		settings.m_VelocityWeightScale =
			overrides.m_VelocityWeightScale.value_or(settings.m_VelocityWeightScale);
		settings.m_LuminanceWeightScale =
			overrides.m_LuminanceWeightScale.value_or(settings.m_LuminanceWeightScale);
		settings.m_NeighborhoodClampExpansion = overrides.m_NeighborhoodClampExpansion.value_or(
			settings.m_NeighborhoodClampExpansion);
		settings.m_HistoryRelaxation =
			overrides.m_HistoryRelaxation.value_or(settings.m_HistoryRelaxation);
		settings.m_HistoryAccumulation =
			overrides.m_HistoryAccumulation.value_or(settings.m_HistoryAccumulation);
		settings.m_HistoryRectification =
			overrides.m_HistoryRectification.value_or(settings.m_HistoryRectification);
		settings.m_VarianceClipGamma =
			overrides.m_VarianceClipGamma.value_or(settings.m_VarianceClipGamma);
		settings.m_HistoryFilter = overrides.m_HistoryFilter.value_or(settings.m_HistoryFilter);
		settings.m_CurrentFilter = overrides.m_CurrentFilter.value_or(settings.m_CurrentFilter);
		settings.m_MotionSelection =
			overrides.m_MotionSelection.value_or(settings.m_MotionSelection);
		settings.m_PostTemporalView =
			overrides.m_PostTemporalView.value_or(settings.m_PostTemporalView);
		settings.m_ResolutionPreset =
			overrides.m_ResolutionPreset.value_or(settings.m_ResolutionPreset);
		settings.m_TextureLodBiasOffset =
			overrides.m_TextureLodBiasOffset.value_or(settings.m_TextureLodBiasOffset);
		return ResolveTemporalAASettings(settings);
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
		if (!m_CaptureJobs.empty())
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

	bool FrameSequenceCoordinator::Cancel() noexcept
	{
		if (!IsActive())
		{
			return false;
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
		return true;
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
		std::span<const CameraPath> cameraPaths) noexcept
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
			m_Status->m_CameraPathVersion = path->m_Version;
			m_Status->m_FrameCount = frameCount;
			m_Status->m_State = FrameSequenceState::Running;
		}

		if (m_Status->m_State != FrameSequenceState::Running)
		{
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

	bool FrameSequenceCoordinator::BeginFrame(const FrameCaptureFrameState& state) noexcept
	{
		if (!m_Status || m_Status->m_State != FrameSequenceState::Running || !m_AppliedPose)
		{
			return true;
		}
		if (!state.m_Readiness.IsReady())
		{
			Fail(std::format("A readiness gate left Ready at sequence frame {}: {}", m_Frame,
				DescribePendingGates(state.m_Readiness)));
			return false;
		}
		if (m_LastSettleKey &&
			!state.m_SettleKey.Continues(*m_LastSettleKey, m_AppliedPose->m_Cut))
		{
			Fail(std::format("Temporal continuity changed at sequence frame {} outside a camera "
				"path cut (effective temporal identity, history availability or content).", m_Frame));
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
		m_BegunRenderFrameSerial = state.m_FrameSerial;
		m_FrameBegun = true;
		return true;
	}

	void FrameSequenceCoordinator::OnFrameSubmitted(uint64_t renderFrameSerial) noexcept
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
		if (auto* watch = m_Capture->GetNonFiniteWatch())
		{
			watch->TrackCoverageFrame(m_WatchCoverageId, renderFrameSerial);
		}
		if (WantsGpuTiming())
		{
			m_PendingGpuTimingFrames.push_back({ .m_RenderFrameSerial = renderFrameSerial,
				.m_SubmissionOrdinal = m_GpuTimingSubmissionCount,
				.m_SequenceFrame = m_Frame, .m_ReferenceSample = m_Sample });
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
			timing.m_FrameMilliseconds.push_back(profile.m_FrameMilliseconds);
			for (const GpuProfileSample& sample : profile.m_Samples)
			{
				auto series = std::ranges::find(
					timing.m_Scopes, sample.m_Name, &FrameSequenceGpuTimingSeries::m_Name);
				if (series == timing.m_Scopes.end())
				{
					timing.m_Scopes.push_back({ .m_Name = sample.m_Name });
					series = std::prev(timing.m_Scopes.end());
				}
				series->m_Milliseconds.push_back(sample.m_Milliseconds);
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
		if (auto* watch = m_Capture->GetNonFiniteWatch())
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
		if (m_Status && m_Status->m_State == FrameSequenceState::Finishing &&
			m_CaptureJobs.empty() &&
			m_PendingGpuTimingFrames.empty() &&
			(!m_Status->m_WatchCoverage || m_Status->m_WatchCoverage->m_Complete))
		{
			m_Status->m_State = FrameSequenceState::Completed;
			if (auto* watch = m_Capture->GetNonFiniteWatch())
			{
				watch->ReleaseCoverage(m_WatchCoverageId);
			}
			m_WatchCoverageId = 0;
		}
	}
}
