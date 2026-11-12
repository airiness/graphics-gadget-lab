#include "Capture/FrameCaptureReadiness.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "CaptureCoordinatorTestCases.h"
#include "CaptureTestServices.h"
#include "FrameCaptureCoordinatorSelfTests.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/CameraPath.h"
#include "GGLabRuntime/Graphics/Pipeline/GIFieldObservation.h"
#include "GGLabRuntime/Graphics/Pipeline/GIFieldReadiness.h"
#include "GGLabTestCore/SelfTest.h"
#include "Lab/LabParameter.h"
#include "Parameters/ParameterBatch.h"
#include "Parameters/ParameterEffectReceipt.h"
#include "SequenceTestServices.h"

#include <cstdint>
#include <memory>
#include <string>

namespace gglab::capture_test
{
	void RunGIFieldLightingRefreshTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("sequence-gi-lighting-refresh");
		const auto setMeasurement = [](GIFieldCaptureState& field, GIFieldSummary& summary,
			bool refresh, uint64_t update)
			{
				field.m_Readiness.m_CompletedUpdates = update;
				auto measurement = std::make_shared<GIFieldReadinessMeasurement>();
				measurement->m_Readiness = field.m_Readiness;
				measurement->m_RefreshMode = refresh;
				auto change = std::make_shared<GIFieldChangeObservation>();
				change->m_Observation = { .m_Field = summary.m_Field,
					.m_ReadinessUpdate = update };
				change->m_Measurement = measurement;
				summary.m_ChangeObservation = change;
				return change;
			};
		for (uint32_t variant = 0; variant < 28; ++variant)
		{
			SequenceHarness harness(directory.GetPath());
			auto initial = MakeFrameState(true);
			auto summary = std::make_shared<GIFieldSummary>();
			summary->m_Field = { 3, 5, 7, 7, 1, 0 };
			summary->m_Initialized = true;
			initial.m_Readiness.m_GIField = GIFieldCaptureState{
				.m_Generation = 3, .m_SceneRevision = 5, .m_LightingGeneration = 7,
				.m_EnvironmentGeneration = 7,
				.m_Readiness = { .m_State = GIFieldReadinessState::Ready, .m_Enabled = true },
				.m_Summary = summary };
			initial.m_Readiness.Add("gi-field", FrameCaptureGateState::Ready, {}, "gi-field", 3);
			std::string error;
			GGLAB_UNUSED(harness.m_Sequence.Start({ .m_CameraPathId = "SEQ_Test",
				.m_Events = { { .m_Frame = 0,
					.m_Values = { { LabParameterId("sun"), int32_t(20000) } } } } }, error));
			const auto pose = harness.m_Sequence.PrepareFrame(&initial, harness.m_Paths, 42,
				[](auto, auto&) { return true; });
			harness.m_Sequence.OnPoseApplied(EvaluateCameraPath(harness.m_Paths.front(), 0));
			ParameterBatchStatus batch{ .m_BatchId = 1, .m_Revision = 2,
				.m_State = ParameterBatchState::Finished,
				.m_Items = { { .m_Parameter = LabParameterId("sun"),
					.m_DeclaredClass = ParameterEffectClass::Deferred,
					.m_State = ParameterEffectState::Consumed,
					.m_Identity = { .m_Revision = 2, .m_Domain = "lighting", .m_Generation = 8 },
					.m_Requested = 100, .m_Applied = 100, .m_Published = 101,
					.m_FirstConsumed = 101 } } };
			auto current = initial;
			auto changed = std::make_shared<GIFieldSummary>(*summary);
			changed->m_Field = { 3, 5, 8, 8, 2, 50 };
			auto& field = *current.m_Readiness.m_GIField;
			field.m_Summary = changed;
			field.m_LightingGeneration = 8;
			field.m_EnvironmentGeneration = 8;
			field.m_Readiness.m_State = GIFieldReadinessState::Settling;
			current.m_Readiness.m_Gates.back().m_State = FrameCaptureGateState::Pending;
			current.m_FrameSerial = 102;
			FrameSequenceLightingState lighting{ 8, 8 };
			switch (variant)
			{
			case 0:
				break;
			case 1:
				lighting.m_RequestedGeneration = 9;
				break;
			case 2:
				lighting.m_BuiltGeneration = 9;
				break;
			case 3:
				changed->m_Field.m_Generation = 4;
				break;
			case 4:
				changed->m_Field.m_SceneRevision = 6;
				break;
			case 5:
				changed->m_Field.m_RefreshEpoch = 1;
				break;
			case 6:
				changed->m_Invalid = true;
				break;
			case 7:
				changed->m_Initialized = false;
				break;
			case 8:
				field.m_LightingGeneration = 7;
				break;
			case 9:
				field.m_Readiness.m_Failed = true;
				break;
			case 10:
				field.m_Readiness.m_Refreshing = false;
				break;
			case 11:
				batch.m_Items[0].m_Identity.m_Generation = 9;
				break;
			case 12:
				batch.m_Items[0].m_Identity.m_Revision = 3;
				break;
			case 13:
				batch.m_Items[0].m_State = ParameterEffectState::NoChange;
				break;
			case 14:
				batch.m_Items[0].m_State = ParameterEffectState::Applied;
				break;
			case 15:
				batch.m_Items[0].m_Identity.m_Domain = "unrelated";
				break;
			case 16:
				current.m_Readiness.m_Gates.back().m_State = FrameCaptureGateState::Failed;
				break;
			case 17:
				batch.m_State = ParameterBatchState::Applying;
				batch.m_Items[0].m_State = ParameterEffectState::Published;
				batch.m_Items[0].m_FirstConsumed.reset();
				break;
			case 18:
			case 19:
				break;
			case 20:
				current.m_Readiness.Add("environment", FrameCaptureGateState::Pending);
				break;
			case 26:
				field.m_Readiness.m_Refreshing = false;
				setMeasurement(field, *changed, false, 34);
				break;
			}
			if (variant != 18)
			{
				harness.m_Sequence.OnParameterEventBatch(batch);
			}
			const bool accepted = harness.m_Sequence.BeginFrame(current, {}, lighting);
			const bool expectRefresh = variant == 0 || variant == 17 || variant == 19 ||
				(variant >= 21 && variant != 26);
			context.Check(pose && accepted == expectRefresh,
				"Only an owned published lighting generation covers its initialized, unchanged field refresh");
			if (!expectRefresh)
			{
				continue;
			}
			context.Check(harness.m_Sequence.GetStatus()->m_SubmittedFrames == 0,
				"Allowing a transition frame neither advances nor claims successful consumption");
			harness.m_Sequence.OnFrameSubmitted(102);
			if (variant >= 21)
			{
				for (uint32_t frame = 1; frame <= 5; ++frame)
				{
					GGLAB_UNUSED(harness.m_Sequence.PrepareFrame(&current, harness.m_Paths, 42));
					harness.m_Sequence.OnPoseApplied(EvaluateCameraPath(harness.m_Paths.front(), frame));
					const bool ready = variant == 25 ? frame == 5 : frame == 1 || frame == 3;
					field.m_Readiness.m_State = ready ?
						GIFieldReadinessState::Ready :
						GIFieldReadinessState::Settling;
					field.m_Readiness.m_Refreshing = variant == 25 && !ready;
					current.m_Readiness.m_Gates.back().m_State = ready ?
						FrameCaptureGateState::Ready : FrameCaptureGateState::Pending;
					auto change = setMeasurement(field, *changed, frame == 1 || variant == 25, 32 + frame);
					if (frame == 2)
					{
						if (variant == 22)
						{
							++change->m_Observation.m_Field.m_RefreshEpoch;
						}
						else if (variant == 23 || variant == 27)
						{
							setMeasurement(field, *changed, true, 34);
							field.m_Readiness.m_Refreshing = variant == 27;
						}
						else if (variant == 24)
						{
							changed->m_ChangeObservation.reset();
						}
					}
					current.m_FrameSerial = 102 + frame;
					const bool allowed = harness.m_Sequence.BeginFrame(current, {}, lighting);
					const uint32_t failureFrame = variant == 25 ? 5 : variant == 21 ? 4 : 2;
					context.Check(allowed == (frame < failureFrame),
						"Only the proven same-epoch refresh-to-steady handoff extends coverage until steady Ready");
					if (!allowed)
					{
						break;
					}
					harness.m_Sequence.OnFrameSubmitted(current.m_FrameSerial);
				}
				continue;
			}
			if (variant == 19)
			{
				for (uint32_t frame = 1; frame < 6; ++frame)
				{
					GGLAB_UNUSED(harness.m_Sequence.PrepareFrame(&current, harness.m_Paths, 42));
					harness.m_Sequence.OnPoseApplied(EvaluateCameraPath(harness.m_Paths.front(), frame));
					current.m_FrameSerial = 102 + frame;
					const bool allowed = harness.m_Sequence.BeginFrame(current, {}, lighting);
					context.Check(allowed == (frame != 5),
						"The final Pending refresh frame fails instead of publishing a completed range");
					if (allowed)
					{
						harness.m_Sequence.OnFrameSubmitted(current.m_FrameSerial);
					}
				}
				continue;
			}
			GGLAB_UNUSED(harness.m_Sequence.PrepareFrame(&current, harness.m_Paths, 42));
			harness.m_Sequence.OnPoseApplied(EvaluateCameraPath(harness.m_Paths.front(), 1));
			field.m_Readiness.m_State = GIFieldReadinessState::Ready;
			current.m_Readiness.m_Gates.back().m_State = FrameCaptureGateState::Ready;
			current.m_FrameSerial = 103;
			context.Check(harness.m_Sequence.BeginFrame(current, {}, lighting),
				"The matching field may finish refresh without skipping any transition frames");
			harness.m_Sequence.OnFrameSubmitted(103);
			GGLAB_UNUSED(harness.m_Sequence.PrepareFrame(&current, harness.m_Paths, 42));
			harness.m_Sequence.OnPoseApplied(EvaluateCameraPath(harness.m_Paths.front(), 2));
			field.m_Readiness.m_State = GIFieldReadinessState::Settling;
			current.m_Readiness.m_Gates.back().m_State = FrameCaptureGateState::Pending;
			context.Check(!harness.m_Sequence.BeginFrame(current, {}, lighting) &&
				harness.m_Sequence.GetStatus()->m_State == FrameSequenceState::Failed,
				"A consumed lighting receipt cannot excuse readiness loss after its refresh reaches Ready");
		}
	}
}

namespace gglab
{
	using namespace capture_test;

	void RunGIFieldLightingRefreshSelfTests(SelfTestContext& context) noexcept
	{
		RunGIFieldLightingRefreshTests(context);
	}
}
