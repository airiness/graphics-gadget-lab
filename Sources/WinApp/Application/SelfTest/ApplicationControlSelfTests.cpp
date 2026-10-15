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
				ParseApplicationControlRequest(R"({"protocol":2,"id":7,"command":"status"})");
			context.Check(status.m_Request && status.m_Request->m_Id == 7 &&
				status.m_Request->m_Command == ApplicationControlCommand::Status,
				"A status request parses with its id");

			const ApplicationControlParseResult capture = ParseApplicationControlRequest(
				R"({"protocol":2,"id":8,"command":"capture","source":"composited",)"
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
				R"({"protocol":2,"id":9,"command":"capture"})");
			context.Check(defaults.m_Request &&
				defaults.m_Request->m_Capture.m_Timing == FrameCaptureTiming::AfterReady &&
				defaults.m_Request->m_Capture.m_SettleFrames == 8 &&
				defaults.m_Request->m_Capture.m_Source == FrameCaptureSource::Scene &&
				defaults.m_Request->m_Wait,
				"A bare capture waits for a settled after-ready scene capture");

			const ApplicationControlParseResult result = ParseApplicationControlRequest(
				R"({"protocol":2,"id":10,"command":"result","requestId":4})");
			context.Check(result.m_Request && result.m_Request->m_CaptureRequestId == 4,
				"A result request names one capture request");

			const ApplicationControlParseResult sequence = ParseApplicationControlRequest(
				R"({"protocol":2,"id":11,"command":"sequence","path":"SEQ_DollyDoorway",)"
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
			const ApplicationControlParseResult diagnosticSequence = ParseApplicationControlRequest(
				R"({"protocol":2,"id":13,"command":"sequence","path":"SEQ_A","source":"diagnostic",)"
				R"("diagnosticTap":"temporal-rejection","referenceSamples":64,)"
				R"("referenceTextureLodBias":-3})");
			const ApplicationControlParseResult diagnosticCapture = ParseApplicationControlRequest(
				R"({"protocol":2,"id":14,"command":"capture","source":"diagnostic",)"
				R"("diagnosticTap":"temporal-history-weight"})");
			context.Check(diagnosticSequence.m_Request &&
				diagnosticSequence.m_Request->m_Sequence.m_CaptureSource ==
				FrameCaptureSource::Diagnostic &&
				diagnosticSequence.m_Request->m_Sequence.m_DiagnosticTap ==
				PostProcessDebugTap::TemporalRejection &&
				diagnosticSequence.m_Request->m_Sequence.m_ReferenceSamples == 64 &&
				diagnosticSequence.m_Request->m_Sequence.m_ReferenceTextureLodBias == -3.0f &&
				diagnosticCapture.m_Request &&
				diagnosticCapture.m_Request->m_Capture.m_DiagnosticTap ==
				PostProcessDebugTap::TemporalHistoryWeight,
				"Diagnostic captures and sequences name their tap");
			const ApplicationControlParseResult evaluation = ParseApplicationControlRequest(
				R"({"protocol":2,"id":15,"command":"sequence","path":"SEQ_A","gpuTiming":true,)"
				R"("temporalAA":{"neighborhoodClampExpansion":1,"maxHistoryFeedback":0.9,)"
				R"("historyFilter":"bilinear","currentFilter":"point",)"
				R"("motionSelection":"center","postTemporalView":"jittered",)"
				R"("resolutionPreset":"quality",)"
				R"("historyRectification":"variance-clip","varianceClipGamma":1.25,)"
				R"("historyAccumulation":"compatibility-age","historyRelaxation":2,)"
				R"("textureLodBiasOffset":-1.5,"enabled":false}})");
			const FrameSequenceTemporalAAOverrides* overrides = evaluation.m_Request
				? &evaluation.m_Request->m_Sequence.m_TemporalAAOverrides
				: nullptr;
			context.Check(overrides && evaluation.m_Request->m_Sequence.m_GpuTiming &&
				overrides->m_NeighborhoodClampExpansion == 1.0f &&
				overrides->m_MaxHistoryFeedback == 0.9f && !overrides->m_VelocityWeightScale &&
				overrides->m_HistoryFilter == TemporalAAHistoryFilter::Bilinear &&
				overrides->m_CurrentFilter == TemporalAACurrentFilter::Point &&
				overrides->m_MotionSelection == TemporalAAMotionSelection::Center &&
				overrides->m_PostTemporalView == TemporalAAPostTemporalView::Jittered &&
				overrides->m_ResolutionPreset == TemporalAAResolutionPreset::Quality &&
				overrides->m_HistoryRectification == TemporalAAHistoryRectification::VarianceClip &&
				overrides->m_VarianceClipGamma == 1.25f &&
				overrides->m_HistoryAccumulation == TemporalAAHistoryAccumulation::CompatibilityAge &&
				overrides->m_HistoryRelaxation == 2.0f &&
				overrides->m_TextureLodBiasOffset == -1.5f && overrides->m_Enabled == false,
				"A sequence request carries its Temporal AA overrides and GPU timing request");
			const ApplicationControlParseResult gtaoEvaluation = ParseApplicationControlRequest(
				R"({"protocol":2,"id":16,"command":"sequence","path":"SEQ_A",)"
				R"("gtao":{"temporal":true,"temporalMaxSamples":24,"directionCount":1,)"
				R"("stepCount":8}})");
			const FrameSequenceGTAOOverrides* gtaoOverrides = gtaoEvaluation.m_Request
				? &gtaoEvaluation.m_Request->m_Sequence.m_GTAOOverrides
				: nullptr;
			context.Check(gtaoOverrides && gtaoOverrides->m_TemporalAccumulation == true &&
				gtaoOverrides->m_TemporalMaxSamples == 24u && gtaoOverrides->m_DirectionCount == 1u &&
				gtaoOverrides->m_StepCount == 8u,
				"A sequence request carries its GTAO overrides");

			const ApplicationControlParseResult cancel = ParseApplicationControlRequest(
				R"({"protocol":2,"id":12,"command":"sequence-cancel"})");
			context.Check(cancel.m_Request &&
				cancel.m_Request->m_Command == ApplicationControlCommand::SequenceCancel,
				"A sequence-cancel request takes no fields");

			const auto graph = ParseApplicationControlRequest(
				R"({"protocol":2,"id":50,"command":"graph","summary":true,"wait":false})");
			context.Check(graph.m_Request && graph.m_Request->m_Command == ApplicationControlCommand::Graph &&
				graph.m_Request->m_Capture.m_Source == FrameCaptureSource::RenderGraph &&
				graph.m_Request->m_Capture.m_GraphSummary && !graph.m_Request->m_Wait,
				"Graph requests reuse capture timing with a typed summary flag");

			const auto resourceExport = ParseApplicationControlRequest(
				R"({"protocol":2,"id":60,"command":"export","targets":[{"resource":"Color","at":{"before":"Repeated"},"occurrence":2,"mip":1,"slices":[2,3]}],"graph":true})");
			context.Check(resourceExport.m_Request && resourceExport.m_Request->m_Command == ApplicationControlCommand::Export &&
				resourceExport.m_Request->m_Capture.m_IncludeGraph && resourceExport.m_Request->m_Capture.m_ResourceTargets.size() == 1 &&
				resourceExport.m_Request->m_Capture.m_ResourceTargets[0].m_Occurrence == 2 &&
				resourceExport.m_Request->m_Capture.m_ResourceTargets[0].m_SliceCount == 3,
				"Resource exports preserve typed addresses and optional same-frame graphs");
			const auto sequenceExport = ParseApplicationControlRequest(
				R"({"protocol":2,"id":61,"command":"sequence","path":"A","exports":[{"frame":1,"targets":[{"resource":"Depth","aspect":"stencil"}],"graph":true}]})");
			context.Check(sequenceExport.m_Request && sequenceExport.m_Request->m_Sequence.m_Exports.size() == 1 &&
				sequenceExport.m_Request->m_Sequence.m_Exports[0].m_Frame == 1 && sequenceExport.m_Request->m_Sequence.m_Exports[0].m_IncludeGraph,
				"Sequence exports retain a complete target group at one selected frame");

			const auto watch = ParseApplicationControlRequest(
				R"({"protocol":2,"id":70,"command":"watch","mode":"report","claims":["boundary"],"targets":[{"resource":"Motion","at":{"before":"TAA"},"occurrence":2,"slices":[2,1]}]})");
			context.Check(watch.m_Request && watch.m_Request->m_Command == ApplicationControlCommand::Watch &&
				!watch.m_Request->m_Watch.m_Production && watch.m_Request->m_Watch.m_Targets.size() == 1 &&
				watch.m_Request->m_Watch.m_Targets[0].m_Address.m_Occurrence == 2u,
				"Watch report requests preserve explicit boundary addresses and requested claims");
			const auto bufferWatch = ParseApplicationControlRequest(
				R"({"protocol":2,"id":71,"command":"watch","targets":[{"resource":"Values","buffer":{"stride":16,"validElementCount":3,"fields":[{"offset":0,"type":"uint32"},{"offset":4,"type":"float32","count":2},{"offset":12,"type":"float16","count":2}]}}]})");
			context.Check(bufferWatch.m_Request && bufferWatch.m_Request->m_Watch.m_Targets[0].m_BufferLayout.m_Components.size() == 4 &&
				bufferWatch.m_Request->m_Watch.m_Targets[0].m_BufferLayout.m_ScalarCount == 12,
				"Buffer watch interpretation owns only float fields within the supplied valid element count");
			const auto watchOff = ParseApplicationControlRequest(R"({"protocol":2,"id":72,"command":"watch","enabled":false})");
			context.Check(watchOff.m_Request && !watchOff.m_Request->m_Watch.m_Enabled,
				"Watch can be disabled without specifying targets or a counter capability");
			const auto productionWatch = ParseApplicationControlRequest(R"({"protocol":2,"id":73,"command":"watch","claims":["production"]})");
			context.Check(productionWatch.m_Request && !productionWatch.m_Request->m_Watch.m_Boundary && productionWatch.m_Request->m_Watch.m_Production,
				"Production-only watch does not fabricate boundary targets");

			struct Rejected
			{
				std::string_view m_Line;
				uint64_t m_Id;
			};
			const Rejected rejected[] = {
				{ R"({"protocol":2,"id":80,"command":"watch","claims":[]})", 80 },
				{ R"({"protocol":2,"id":81,"command":"watch","claims":["boundary","boundary"]})", 81 },
				{ R"({"protocol":2,"id":82,"command":"watch","claims":["other"]})", 82 },
				{ R"({"protocol":2,"id":83,"command":"watch","mode":"fatal"})", 83 },
				{ R"({"protocol":2,"id":84,"command":"watch","enabled":1})", 84 },
				{ R"({"protocol":2,"id":85,"command":"watch","enabled":false,"targets":[]})", 85 },
				{ R"({"protocol":2,"id":86,"command":"watch","target":[]})", 86 },
				{ R"({"protocol":2,"id":87,"command":"watch","targets":[null]})", 87 },
				{ R"({"protocol":2,"id":88,"command":"watch","targets":[{"resource":"A","buffer":{}}]})", 88 },
				{ R"({"protocol":2,"id":89,"command":"watch","targets":[{"resource":"A","buffer":{"stride":4,"validElementCount":2,"fields":[{"offset":0,"type":"uint32"}]}}]})", 89 },
				{ R"({"protocol":2,"id":90,"command":"watch","targets":[{"resource":"A","buffer":{"stride":4,"validElementCount":-1,"fields":[{"offset":0,"type":"float32"}]}}]})", 90 },
				{ R"({"protocol":2,"id":91,"command":"watch","targets":[{"resource":"A","buffer":{"stride":4,"validElementCount":1,"fields":[{"offset":0,"type":"float32","count":4294967295}]}}]})", 91 },
				{ R"({"protocol":2,"id":92,"command":"watch","targets":[{"resource":"A","buffer":{"stride":8,"validElementCount":1,"fields":[{"offset":0,"type":"float32","count":2},{"offset":4,"type":"float32"}]}}]})", 92 },
				{ R"({"protocol":2,"id":93,"command":"watch","targets":[{"resource":"A","buffer":{"stride":4,"validElementCount":1,"fields":[{"offset":0,"type":"float32","extra":1}]}}]})", 93 },
				{ R"({"protocol":2,"id":62,"command":"export","targets":[]})", 62 },
				{ R"({"protocol":2,"id":63,"command":"export","targets":[{"resource":"A","at":{"before":"P","after":"P"}}]})", 63 },
				{ R"({"protocol":2,"id":64,"command":"export","targets":[{"resource":"A","occurrence":0}]})", 64 },
				{ R"({"protocol":2,"id":65,"command":"export","targets":[{"resource":"A","slices":[0,0]}]})", 65 },
				{ R"({"protocol":2,"id":66,"command":"export","targets":[{"resource":"A","mipp":1}]})", 66 },
				{ R"({"protocol":2,"id":67,"command":"export","targets":[{"resource":"A","aspect":"all"}]})", 67 },
				{ R"({"protocol":2,"id":68,"command":"export","targets":[{"resource":"A"}],"graph":1})", 68 },
				{ "not json", 0 },
				{ R"([1,2])", 0 },
				{ R"({"protocol":2,"command":"status"})", 0 },
				{ R"({"protocol":1,"id":3,"command":"status"})", 3 },
				{ R"({"protocol":2,"id":4,"command":"reboot"})", 4 },
				{ R"({"protocol":2,"id":5,"command":"status","extra":1})", 5 },
				{ R"({"protocol":2,"id":6,"command":"capture","source":"hud"})", 6 },
				{ R"({"protocol":2,"id":7,"command":"capture","settleFrames":-1})", 7 },
				{ R"({"protocol":2,"id":8,"command":"capture","outputDirectory":"relative"})", 8 },
				{ R"({"protocol":2,"id":9,"command":"capture","lable":"typo"})", 9 },
				{ R"({"protocol":2,"id":10,"command":"result"})", 10 },
				{ R"({"protocol":2,"id":11,"command":"result","requestId":0})", 11 },
				{ R"({"protocol":2,"id":12,"command":"sequence"})", 12 },
				{ R"({"protocol":2,"id":13,"command":"sequence","path":"A","captureFrames":[-1]})", 13 },
				{ R"({"protocol":2,"id":14,"command":"sequence","path":"A","captureFrames":3})", 14 },
				{ R"({"protocol":2,"id":15,"command":"sequence","path":"A","frames":[1]})", 15 },
				{ R"({"protocol":2,"id":16,"command":"sequence-cancel","path":"A"})", 16 },
				{ R"({"protocol":2,"id":17,"command":"capture","source":"diagnostic"})", 17 },
				{ R"({"protocol":2,"id":20,"command":"sequence","path":"A","referenceSamples":5000})", 20 },
				{ R"({"protocol":2,"id":18,"command":"capture","diagnosticTap":"temporal-rejection"})", 18 },
				{ R"({"protocol":2,"id":19,"command":"sequence","path":"A","source":"diagnostic","diagnosticTap":"bloom-result"})", 19 },
				{ R"({"protocol":2,"id":21,"command":"sequence","path":"A","temporalAA":{"maxHistoryFeedback":1.5}})", 21 },
				{ R"({"protocol":2,"id":22,"command":"sequence","path":"A","temporalAA":{"historyFilter":1}})", 22 },
				{ R"({"protocol":2,"id":26,"command":"sequence","path":"A","temporalAA":{"historyFilter":"bicubic"}})", 26 },
				{ R"({"protocol":2,"id":27,"command":"sequence","path":"A","temporalAA":{"historyFilter":"catmull-rom"}})", 27 },
				{ R"({"protocol":2,"id":28,"command":"sequence","path":"A","temporalAA":{"currentFilter":"box"}})", 28 },
				{ R"({"protocol":2,"id":29,"command":"sequence","path":"A","temporalAA":{"currentFilter":"gaussian-narrow"}})", 29 },
				{ R"({"protocol":2,"id":30,"command":"sequence","path":"A","temporalAA":{"motionSelection":"dilated"}})", 30 },
				{ R"({"protocol":2,"id":31,"command":"sequence","path":"A","temporalAA":{"motionSelection":"closest-depth-correspondence"}})", 31 },
				{ R"({"protocol":2,"id":32,"command":"sequence","path":"A","temporalAA":{"textureLodBiasOffset":-3}})", 32 },
				{ R"({"protocol":2,"id":35,"command":"sequence","path":"A","temporalAA":{"postTemporalView":"none"}})", 35 },
				{ R"({"protocol":2,"id":36,"command":"sequence","path":"A","temporalAA":{"resolutionPreset":"balanced"}})", 36 },
				{ R"({"protocol":2,"id":37,"command":"sequence","path":"A","temporalAA":{"historyRectification":"variance"}})", 37 },
				{ R"({"protocol":2,"id":38,"command":"sequence","path":"A","temporalAA":{"varianceClipGamma":8}})", 38 },
				{ R"({"protocol":2,"id":39,"command":"sequence","path":"A","temporalAA":{"historyAccumulation":"age"}})", 39 },
				{ R"({"protocol":2,"id":40,"command":"sequence","path":"A","temporalAA":{"historyRelaxation":5}})", 40 },
				{ R"({"protocol":2,"id":33,"command":"sequence","path":"A","referenceTextureLodBias":"low"})", 33 },
				{ R"({"protocol":2,"id":34,"command":"sequence","path":"A","temporalAA":{"enabled":0}})", 34 },
				{ R"({"protocol":2,"id":23,"command":"sequence","path":"A","temporalAA":{"velocityWeightScale":"x"}})", 23 },
				{ R"({"protocol":2,"id":24,"command":"sequence","path":"A","temporalAA":[]})", 24 },
				{ R"({"protocol":2,"id":25,"command":"sequence","path":"A","gpuTiming":1})", 25 },
				{ R"({"protocol":2,"id":41,"command":"sequence","path":"A","gtao":{"temporal":1}})", 41 },
				{ R"({"protocol":2,"id":42,"command":"sequence","path":"A","gtao":{"temporalMaxSamples":0}})", 42 },
				{ R"({"protocol":2,"id":43,"command":"sequence","path":"A","gtao":{"temporalMaxSamples":2.5}})", 43 },
				{ R"({"protocol":2,"id":44,"command":"sequence","path":"A","gtao":{"radius":2}})", 44 },
				{ R"({"protocol":2,"id":45,"command":"sequence","path":"A","gtao":{"directionCount":9}})", 45 },
				{ R"({"protocol":2,"id":51,"command":"graph","summary":1})", 51 },
				{ R"({"protocol":2,"id":52,"command":"graph","source":"scene"})", 52 },
				{ R"({"protocol":2,"id":53,"command":"graph","extra":true})", 53 },
				{ R"({"protocol":2,"id":54,"command":"capture","summary":true})", 54 },
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
			context.Check(Contains(error, R"("protocol":2)") && Contains(error, R"("id":3)") &&
				Contains(error, R"("ok":false)") && Contains(error, R"(bad \"input\")"),
				"Errors are versioned JSON with an escaped reason");

			NonFiniteWatchStatus watch{ .m_ConfigurationVersion = 4, .m_Enabled = true,
				.m_BoundaryRequested = true, .m_ProductionRequested = false, .m_BoundaryFrameSerial = 123,
				.m_Boundary = { NonFiniteClaimStatus::Failed, NonFiniteWatchFailure::NonFiniteDetected } };
			NonFiniteWatchTargetStatus target{ .m_Address = { .m_Resource = "Motion", .m_Mip = 1 },
				.m_Record = { .m_CheckedCount = 8, .m_NanCount = 1 },
				.m_Result = { .m_Claim = watch.m_Boundary, .m_FirstNonFiniteIndex = 0 },
				.m_TextureLocation = NonFiniteTextureLocation{ 0, 0, 2, 0 } };
			watch.m_Targets.push_back(target);
			watch.m_FirstAnomaly = { .m_FrameSerial = 123, .m_Failure = NonFiniteWatchFailure::NonFiniteDetected, .m_Target = target };
			const auto watchJson = SerializeApplicationControlWatch(4, watch, false);
			context.Check(Contains(watchJson, R"("configurationVersion":4)") && Contains(watchJson, R"("boundaryFrameSerial":123)") &&
				Contains(watchJson, R"("coverage":"latest-completed-frame")") && Contains(watchJson, R"("firstIndex":0)") &&
				Contains(watchJson, R"("slice":2)") && Contains(watchJson, R"("reason":"NonFiniteDetected")"),
				"Watch JSON carries configuration, separate frame identities and exact first anomaly location");
			context.Check(Contains(SerializeApplicationControlStatus(5, { .m_Watch = &watch }), R"("report":{"boundary":"Failed")"),
				"Session status includes the same watch report as configuration responses");

			watch.m_DefaultTargets = true;
			watch.m_Targets = { NonFiniteWatchTargetStatus{ .m_Semantic = NonFiniteWatchSemantic::AmbientOcclusion,
				.m_Result = { .m_Claim = { NonFiniteClaimStatus::NotApplicable, NonFiniteWatchFailure::None } } } };
			watch.m_FirstAnomaly.reset();
			const auto defaultsJson = SerializeApplicationControlWatch(6, watch, false);
			context.Check(Contains(defaultsJson, R"("targetsMode":"defaults")") && Contains(defaultsJson, R"("defaultTiers":["A","B"])") &&
				Contains(defaultsJson, R"("semantic":"ambient-occlusion")") && Contains(defaultsJson, R"("address":null)") &&
				Contains(defaultsJson, R"("claim":"NotApplicable")"),
				"Default reports identify their tier and inactive semantic roles without inventing an address");
			watch.m_Targets = { NonFiniteWatchTargetStatus{ .m_Semantic = NonFiniteWatchSemantic::Specular,
				.m_Address = { .m_Resource = "IBL.Specular", .m_Mip = 4, .m_SliceCount = 6 },
				.m_Generation = { { 12, 3 }, 9 }, .m_CheckedFrameSerial = 77 } };
			const auto generationJson = SerializeApplicationControlWatch(7, watch, false);
			context.Check(Contains(generationJson, R"("semantic":"ibl.specular")") && Contains(generationJson, R"("watchTier":2)") &&
				Contains(generationJson, R"("lastCheckedFrameSerial":77)") && Contains(generationJson, R"("resourceIdentity":{"generation":3,"index":12})"),
				"Generation reports distinguish content revision, physical resource identity, mip and last checked frame");

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

			const auto availableWatch = SerializeApplicationControlStatus(6, { .m_NonFiniteInstrumentation = true });
			context.Check(Contains(noFrame, R"("instrumentation":"unavailable")") && Contains(noFrame, R"("production":"Unavailable")") &&
				Contains(availableWatch, R"("instrumentation":"available")") && Contains(availableWatch, R"("production":"Incomplete")"),
				"Production capability availability never implies completed frame coverage");

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
				Contains(statusWithSequence, R"("submittedFrames":12)") &&
				!Contains(sequenceJson, R"("gpuTiming")"),
				"Sequence responses and status report the sequence state, progress and failure");

			FrameSequenceStatus timedStatus{ .m_SequenceId = 4, .m_State = FrameSequenceState::Running };
			timedStatus.m_GpuTiming = FrameSequenceGpuTiming{
				.m_FrameMilliseconds = { 4.0, 2.0 },
				.m_Scopes = { { .m_Name = "PostProcess.TemporalAA", .m_Milliseconds = { 0.25 } } },
			};
			const std::string timedJson = SerializeApplicationControlSequence(12, timedStatus);
			context.Check(Contains(timedJson, R"("gpuTiming":{)") &&
				Contains(timedJson, R"("frames":2)") && Contains(timedJson, R"("medianMs":2.0)") &&
				Contains(timedJson, R"("name":"PostProcess.TemporalAA")") &&
				Contains(timedJson, R"("p90Ms":0.25)"),
				"Sequence status summarizes the GPU timing of each recorded scope");

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
