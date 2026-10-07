#include "Capture/FrameSequenceCoordinator.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <string_view>
#include <utility>

namespace gglab
{
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

		// A cut legitimately changes only the camera reset serial.
		[[nodiscard]] bool IsContinuous(const FrameCaptureSettleKey& previous,
			const FrameCaptureSettleKey& current, bool cut) noexcept
		{
			FrameCaptureSettleKey expected = previous;
			if (cut)
			{
				expected.m_CameraResetSerial = current.m_CameraResetSerial;
			}
			return expected == current;
		}
	}

	const char* GetFrameSequenceStateName(FrameSequenceState state) noexcept
	{
		switch (state)
		{
		case FrameSequenceState::Waiting: return "waiting";
		case FrameSequenceState::Running: return "running";
		case FrameSequenceState::Finishing: return "finishing";
		case FrameSequenceState::Completed: return "completed";
		case FrameSequenceState::Failed: return "failed";
		case FrameSequenceState::Cancelled: return "cancelled";
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
		if (request.m_CameraPathId.empty())
		{
			outError = "A sequence requires a camera path id.";
			return 0;
		}
		if ((request.m_CaptureSource == FrameCaptureSource::Diagnostic) !=
			request.m_DiagnosticTap.has_value())
		{
			outError = "A diagnostic tap is required for, and only valid with, diagnostic captures.";
			return 0;
		}
		if (request.m_ReferenceSamples > MaxTemporalReferenceSamples)
		{
			outError = std::format("A reference uses at most {} samples per frame.",
				MaxTemporalReferenceSamples);
			return 0;
		}
		std::ranges::sort(request.m_CaptureFrames);
		const auto duplicates = std::ranges::unique(request.m_CaptureFrames);
		request.m_CaptureFrames.erase(duplicates.begin(), duplicates.end());
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
		};
		m_Frame = 0;
		m_Sample = 0;
		m_AppliedPose.reset();
		m_FrameBegun = false;
		m_CapturedFrame.reset();
		m_LastSettleKey.reset();
		return m_Status->m_SequenceId;
	}

	bool FrameSequenceCoordinator::Cancel() noexcept
	{
		if (!IsActive())
		{
			return false;
		}
		m_Status->m_State = FrameSequenceState::Cancelled;
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
		// Unfinished requests bound every capture that can still reach the writer, so
		// staying below the pending-job limit keeps the writer queue from overflowing.
		return m_Status && m_Status->m_State == FrameSequenceState::Running && IsCaptureDue() &&
			m_Capture->GetUnfinishedRequestCount() >=
			FrameCaptureCoordinator::MaxPendingWriteJobs;
	}

	bool FrameSequenceCoordinator::ShouldHoldTime() const noexcept
	{
		return m_Status && m_Status->m_State == FrameSequenceState::Running && m_Sample > 0;
	}

	bool FrameSequenceCoordinator::IsCaptureDue() const noexcept
	{
		const bool lastSample = m_Request.m_ReferenceSamples == 0 ||
			m_Sample + 1 == m_Request.m_ReferenceSamples;
		return lastSample && m_CapturedFrame != m_Frame &&
			std::ranges::binary_search(m_Request.m_CaptureFrames, m_Frame);
	}

	std::optional<FrameSequencePoseRequest> FrameSequenceCoordinator::PrepareFrame(
		const FrameCaptureFrameState* previousFrame,
		std::span<const CameraPath> cameraPaths) noexcept
	{
		m_AppliedPose.reset();
		m_FrameBegun = false;
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
			.m_ReferenceSample = m_Request.m_ReferenceSamples == 0
				? std::nullopt
				: std::optional(TemporalReferenceSample{
					.m_Index = m_Sample,
					.m_Count = m_Request.m_ReferenceSamples,
				}),
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

	void FrameSequenceCoordinator::BeginFrame(const FrameCaptureFrameState& state) noexcept
	{
		if (!m_Status || m_Status->m_State != FrameSequenceState::Running || !m_AppliedPose)
		{
			return;
		}
		if (!state.m_Readiness.IsReady())
		{
			Fail(std::format("A readiness gate left Ready at sequence frame {}: {}", m_Frame,
				DescribePendingGates(state.m_Readiness)));
			return;
		}
		if (m_LastSettleKey &&
			!IsContinuous(*m_LastSettleKey, state.m_SettleKey, m_AppliedPose->m_Cut))
		{
			Fail(std::format("Temporal continuity changed at sequence frame {} outside a camera "
				"path cut (temporal session, display view, size or content).", m_Frame));
			return;
		}
		m_LastSettleKey = state.m_SettleKey;

		if (IsCaptureDue())
		{
			const uint64_t requestId = m_Capture->Submit(FrameCaptureRequest{
				.m_Source = m_Request.m_CaptureSource,
				.m_Timing = FrameCaptureTiming::NextFrame,
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
				},
				.m_DiagnosticTap = m_Request.m_DiagnosticTap,
				});
			m_Status->m_CaptureRequestIds.push_back(requestId);
			m_CapturedFrame = m_Frame;
		}
		m_FrameBegun = true;
	}

	void FrameSequenceCoordinator::OnFrameSubmitted() noexcept
	{
		if (!m_FrameBegun || !m_Status || m_Status->m_State != FrameSequenceState::Running)
		{
			return;
		}
		m_FrameBegun = false;
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
			CompleteIfFinished();
		}
	}

	void FrameSequenceCoordinator::OnCaptureResults(
		std::span<const FrameCaptureRequestResult> results) noexcept
	{
		if (!m_Status)
		{
			return;
		}
		const std::vector<uint64_t>& ids = m_Status->m_CaptureRequestIds;
		for (const FrameCaptureRequestResult& result : results)
		{
			const auto id = std::ranges::find(ids, result.m_RequestId);
			if (id == ids.end())
			{
				continue;
			}
			if (result.m_Status == FrameCaptureRequestStatus::Completed)
			{
				++m_Status->m_CompletedCaptures;
				continue;
			}
			const uint32_t frame =
				m_Request.m_CaptureFrames[static_cast<size_t>(std::distance(ids.begin(), id))];
			Fail(std::format("Capture of sequence frame {} {}: {}", frame,
				result.m_Status == FrameCaptureRequestStatus::Cancelled ? "was cancelled"
																		 : "failed",
				result.m_Failure));
		}
		CompleteIfFinished();
	}

	void FrameSequenceCoordinator::Fail(std::string failure) noexcept
	{
		if (!m_Status || m_Status->IsTerminal())
		{
			return;
		}
		m_Status->m_State = FrameSequenceState::Failed;
		m_Status->m_Failure = std::move(failure);
	}

	void FrameSequenceCoordinator::CompleteIfFinished() noexcept
	{
		if (m_Status && m_Status->m_State == FrameSequenceState::Finishing &&
			m_Status->m_CompletedCaptures == m_Status->m_CaptureRequestIds.size())
		{
			m_Status->m_State = FrameSequenceState::Completed;
		}
	}
}
