#pragma once

#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePublisher.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "Capture/NonFiniteWatchCoordinator.h"
#include "CaptureTestServices.h"
#include "GGLabRuntime/Graphics/CameraPath.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalReference.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabRuntime/Graphics/ViewRenderSettingsPatch.h"
#include "Parameters/HostViewParameters.h"
#include "Parameters/ParameterFrameState.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace gglab::capture_test
{
	[[nodiscard]] inline CameraPath MakeSequenceTestPath()
	{
		const auto key = [](uint32_t frame, float x, bool cut = false)
		{
			return CameraPathKey{ .m_Frame = frame, .m_Position = { x, 1.0f, 0.0f },
				.m_Target = { x, 1.0f, 10.0f }, .m_Cut = cut };
		};
		return {
			.m_Id = "SEQ_Test",
			.m_Name = "Test",
			.m_Version = 2,
			.m_Keys = { key(0, 0.0f), key(3, 3.0f), key(4, 40.0f, true), key(5, 41.0f) },
		};
	}

	// Stands in for the runtime frame loop: poses the frame, builds its state,
	// issues captures, submits it and completes every issued Runtime capture.
	struct SequenceHarness
	{
		explicit SequenceHarness(const std::filesystem::path& directory,
			CaptureJobLimits limits = {},
			std::shared_ptr<CapturePublicationFileSystemBase> files = {},
			bool writeOnCallingThread = true, bool withWatch = false,
			std::chrono::milliseconds shutdownTimeout = std::chrono::seconds(30)) noexcept :
			m_Capture({
				.m_Capture = &m_Control,
				.m_DefaultOutputDirectory = directory,
				.m_ImageEncoder = &EncodeTestPng,
				.m_ShutdownWriteTimeout = shutdownTimeout,
				.m_WriteOnCallingThread = writeOnCallingThread,
				.m_NonFiniteWatch = withWatch ? &m_Watch : nullptr,
				.m_PublicationFileSystem = std::move(files),
				.m_JobLimits = limits,
			}),
			m_Sequence(m_Capture)
		{
			m_Paths.push_back(MakeSequenceTestPath());
		}

		void CollectCaptureJobs()
		{
			if (!m_CollectJobs)
			{
				return;
			}
			m_Sequence.UpdateCaptureJobs([this](uint64_t jobId)
			{
				auto result = m_Capture.TakeJobResult(jobId);
				if (result)
				{
					m_JobResults.push_back(*result);
					m_Results.insert(m_Results.end(), result->m_Products.begin(), result->m_Products.end());
				}
				return result;
			});
		}

		// Renders one frame; returns the posed sequence frame, if any.
		std::optional<uint32_t> Frame(bool submit = true, uint64_t temporalSession = 1)
		{
			m_Deferred = false;
			if (m_Sequence.ShouldDeferFrame())
			{
				// The runtime neither simulates nor renders a deferred frame.
				m_Deferred = true;
				m_Capture.Update();
				CollectCaptureJobs();
				return std::nullopt;
			}
			if (!m_Sequence.ShouldHoldTime())
			{
				m_SimulationTime += 1.0 / 60.0;
			}
			const std::optional<FrameSequencePoseRequest> pose = m_Sequence.PrepareFrame(m_Capture.GetLastFrameState(), m_Paths);
			m_LastSample = pose ? pose->m_ReferenceSample : std::nullopt;
			m_LastViewPatch = pose ?
				std::optional(MakeHostViewParameterPatch(pose->m_Parameters)) : std::nullopt;
			if (pose)
			{
				const std::optional<CameraPathPose> applied = EvaluateCameraPath(m_Paths.front(), pose->m_Frame);
				if (applied && applied->m_Cut)
				{
					++m_CameraResetSerial;
				}
				m_Sequence.OnPoseApplied(applied);
			}
			FrameCaptureFrameState state = MakeFrameState(m_Ready, temporalSession);
			state.m_FrameSerial = ++m_RenderSerial;
			state.m_TotalTime = m_SimulationTime;
			state.m_SettleKey.m_Temporal.m_Identity.m_ResetIdentity = m_CameraResetSerial;
			state.m_SettleKey.m_Temporal.m_Identity.m_DepthExtent = m_RenderExtent;
			state.m_SettleKey.m_Temporal.m_Identity.m_Accumulation = m_Accumulation;
			state.m_SettleKey.m_Temporal.m_HasCompatiblePreviousHistory = m_HistoryCompatible;
			if (m_LastSample)
			{
				state.m_SettleKey.m_Temporal.m_Services = TemporalService::ProjectionJitter;
				state.m_SettleKey.m_Temporal.m_ReferenceActive = true;
			}
			if (m_BeforeBeginFrame)
			{
				m_BeforeBeginFrame();
			}
			if (!m_Sequence.BeginFrame(state))
			{
				m_Deferred = true;
				CollectCaptureJobs();
				return std::nullopt;
			}
			m_Capture.BeginFrame(std::move(state));
			if (m_LateDeferrals != 0 && std::ranges::any_of(m_Control.m_Issued, [&](const auto& issued)
				{
					return issued.m_FrameSerial == m_RenderSerial && issued.m_Source == FrameCaptureSource::ResourceExport;
				}))
			{
				--m_LateDeferrals;
				for (const auto& issued : m_Control.m_Issued)
				{
					if (issued.m_FrameSerial == m_RenderSerial)
					{
						m_Control.Busy(issued.m_Id);
					}
				}
				m_Capture.Update();
				CollectCaptureJobs();
				m_Completed = m_Control.m_Issued.size();
				m_Deferred = true;
				return pose ? std::optional<uint32_t>(pose->m_Frame) : std::nullopt;
			}
			if (submit)
			{
				if (auto* watch = m_Capture.GetNonFiniteWatch())
				{
					watch->OnFrameSubmitted(m_RenderSerial);
				}
				m_Capture.OnFrameSubmitted();
				const FrameSequenceObservation observation = m_Sequence.WantsGpuTiming() ?
					FrameSequenceObservation{ m_Capture.HasFrameGraphExports(m_RenderSerial), {},
						static_cast<double>(m_RenderSerial) * 0.125 } :
					FrameSequenceObservation{};
				const auto parameters = m_FreezeParameters ?
					std::make_shared<ParameterFrameState>(ParameterFrameState{
						.m_FrameSerial = m_RenderSerial, .m_Revision = m_RenderSerial + 1,
						.m_ContentId = "Fixture", .m_Available = true }) : nullptr;
				m_Capture.SetFrameParameters(m_RenderSerial, parameters);
				m_Sequence.OnFrameSubmitted(m_RenderSerial, observation, parameters);
			}
			for (; m_AutoComplete && m_Completed < m_Control.m_Issued.size(); ++m_Completed)
			{
				m_Control.Complete(m_Control.m_Issued[m_Completed].m_Id);
			}
			m_Capture.Update();
			CollectCaptureJobs();
			return pose ? std::optional<uint32_t>(pose->m_Frame) : std::nullopt;
		}

		FakeCaptureControl m_Control;
		NonFiniteWatchCoordinator m_Watch{ &m_Control };
		FrameCaptureCoordinator m_Capture;
		FrameSequenceCoordinator m_Sequence;
		std::vector<CameraPath> m_Paths;
		uint64_t m_CameraResetSerial = 1;
		uint64_t m_RenderSerial = 0;
		double m_SimulationTime = 0.0;
		uint32_t m_LateDeferrals = 0;
		bool m_FreezeParameters = false;
		ViewExtent m_RenderExtent{ 64, 32 };
		TemporalAAHistoryAccumulation m_Accumulation = TemporalAAHistoryAccumulation::EffectiveSamples;
		bool m_HistoryCompatible = true;
		size_t m_Completed = 0;
		bool m_Ready = true;
		bool m_AutoComplete = true;
		bool m_Deferred = false;
		bool m_CollectJobs = true;
		std::optional<TemporalReferenceSample> m_LastSample;
		std::optional<ViewRenderSettingsPatch> m_LastViewPatch;
		std::vector<CaptureProductResult> m_Results;
		std::vector<CaptureJobResult> m_JobResults;
		std::function<void()> m_BeforeBeginFrame;
	};
}
