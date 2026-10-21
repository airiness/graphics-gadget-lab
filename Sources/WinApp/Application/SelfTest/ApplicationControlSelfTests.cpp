#include "Application/SelfTest/ApplicationControlSelfTests.h"

#include "Application/Control/ApplicationControlCaptureJobs.h"
#include "Application/Control/ApplicationControlProtocol.h"
#include "Application/Platform/Windows/Win32NamedPipeServer.h"
#include "Capture/CaptureBundle.h"
#include "Capture/CaptureEnvironment.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <windows.h>

namespace gglab
{
	namespace
	{
		using Json = nlohmann::json;

		void RunParseTests(SelfTestContext& context) noexcept
		{
			const auto status = ParseApplicationControlRequest(R"({"protocol":3,"id":7,"command":"status"})");
			context.Check(status.m_Request && status.m_Request->m_Id == 7 &&
				status.m_Request->m_Command == ApplicationControlCommand::Status,
				"A v3 status request parses with its id");
			const auto capture = ParseApplicationControlRequest(
				R"({"protocol":3,"id":8,"command":"capture","source":"composited",)"
				R"("timing":"next-frame","settleFrames":3,"requiredContentId":"gglab.lab.culling",)"
				R"("view":"CAM_Courtyard","outputDirectory":"D:/captures","label":"a","note":"b"})");
			context.Check(capture.m_Request && capture.m_Request->m_Capture.m_RequiredProducts.size() == 1 &&
				std::get<ImageCaptureProduct>(capture.m_Request->m_Capture.m_RequiredProducts[0]).m_Source == CaptureImageSource::Composited &&
				capture.m_Request->m_Capture.m_Timing == FrameCaptureTiming::NextFrame &&
				capture.m_Request->m_Capture.m_SettleFrames == 3 &&
				capture.m_Request->m_Capture.m_RequiredContentId == "gglab.lab.culling" &&
				capture.m_Request->m_Capture.m_ReferenceViewId == "CAM_Courtyard" &&
				capture.m_Request->m_Capture.m_OutputDirectory == std::filesystem::path("D:/captures") &&
				capture.m_Request->m_Capture.m_Label == "a" && capture.m_Request->m_Capture.m_Note == "b",
				"Capture directly builds a typed plan and preserves every admission field");
			const auto defaults = ParseApplicationControlRequest(R"({"protocol":3,"id":9,"command":"capture"})");
			context.Check(defaults.m_Request && defaults.m_Request->m_Capture.m_Timing == FrameCaptureTiming::AfterReady &&
				defaults.m_Request->m_Capture.m_SettleFrames == 8 &&
				std::get<ImageCaptureProduct>(defaults.m_Request->m_Capture.m_RequiredProducts[0]).m_Source == CaptureImageSource::Scene &&
				defaults.m_Request->m_Capture.m_WaitForCapacity,
				"Bare capture builds one settled scene product that waits for temporary execution capacity");
			const auto diagnostic = ParseApplicationControlRequest(
				R"({"protocol":3,"id":14,"command":"capture","source":"diagnostic","diagnosticTap":"temporal-history-weight"})");
			context.Check(diagnostic.m_Request &&
				std::get<DiagnosticCaptureProduct>(diagnostic.m_Request->m_Capture.m_RequiredProducts[0]).m_Tap == PostProcessDebugTap::TemporalHistoryWeight,
				"Diagnostic convenience capture directly builds its typed tap");
			const auto graph = ParseApplicationControlRequest(R"({"protocol":3,"id":50,"command":"graph","summary":true})");
			context.Check(graph.m_Request && graph.m_Request->m_Command == ApplicationControlCommand::Graph &&
				std::get<GraphCaptureProduct>(graph.m_Request->m_Capture.m_RequiredProducts[0]).m_Summary,
				"Graph builds a required graph product with a typed summary flag");
			const auto resource = ParseApplicationControlRequest(
				R"({"protocol":3,"id":60,"command":"export","targets":[{"resource":"Color","at":{"before":"Repeated"},"occurrence":2,"mip":1,"slices":[2,3]}],"graph":true})");
			context.Check(resource.m_Request && resource.m_Request->m_Capture.m_RequiredProducts.size() == 2 &&
				std::holds_alternative<GraphCaptureProduct>(resource.m_Request->m_Capture.m_RequiredProducts[1]) &&
				std::get<ResourceCaptureProduct>(resource.m_Request->m_Capture.m_RequiredProducts[0]).m_Targets[0].m_Occurrence == 2 &&
				std::get<ResourceCaptureProduct>(resource.m_Request->m_Capture.m_RequiredProducts[0]).m_Targets[0].m_SliceCount == 3,
				"Export builds two required products instead of reconstructing a legacy group");
			const auto multi = ParseApplicationControlRequest(
				R"({"protocol":3,"id":100,"command":"capture","products":[{"kind":"image","source":"scene"},{"kind":"diagnostic","diagnosticTap":"temporal-rejection"},{"kind":"graph"},{"kind":"resource","targets":[{"resource":"Color"}]}]})");
			context.Check(multi.m_Request && multi.m_Request->m_Capture.m_RequiredProducts.size() == 4 &&
				std::holds_alternative<ImageCaptureProduct>(multi.m_Request->m_Capture.m_RequiredProducts[0]) &&
				std::holds_alternative<DiagnosticCaptureProduct>(multi.m_Request->m_Capture.m_RequiredProducts[1]) &&
				std::holds_alternative<GraphCaptureProduct>(multi.m_Request->m_Capture.m_RequiredProducts[2]) &&
				std::holds_alternative<ResourceCaptureProduct>(multi.m_Request->m_Capture.m_RequiredProducts[3]),
				"Explicit required products retain plan order across all supported kinds");
			const auto query = ParseApplicationControlRequest(R"({"protocol":3,"id":10,"command":"result","jobId":4})");
			const auto consume = ParseApplicationControlRequest(R"({"protocol":3,"id":11,"command":"result","jobId":4,"consume":true})");
			const auto cancel = ParseApplicationControlRequest(R"({"protocol":3,"id":12,"command":"capture-cancel","jobId":4})");
			context.Check(query.m_Request && query.m_Request->m_JobId == 4 && !query.m_Request->m_Consume &&
				consume.m_Request && consume.m_Request->m_Consume && cancel.m_Request &&
				cancel.m_Request->m_Command == ApplicationControlCommand::CaptureCancel,
				"Query, explicit consumption and delivery cancellation share a job identity");

			const auto sequence = ParseApplicationControlRequest(
				R"({"protocol":3,"id":11,"command":"sequence","path":"SEQ_DollyDoorway","requiredContentId":"Demo.CoastalAtrium","captureFrames":[0,90,179],"source":"composited","outputDirectory":"D:/captures","label":"dolly","note":"baseline","exports":[{"frame":1,"targets":[{"resource":"Depth","aspect":"stencil"}],"graph":true}]})");
			context.Check(sequence.m_Request && sequence.m_Request->m_Sequence.m_CameraPathId == "SEQ_DollyDoorway" &&
				sequence.m_Request->m_Sequence.m_RequiredContentId == "Demo.CoastalAtrium" &&
				sequence.m_Request->m_Sequence.m_CaptureFrames == std::vector<uint32_t>{ 0, 90, 179 } &&
				sequence.m_Request->m_Sequence.m_CaptureSource == FrameCaptureSource::Composited &&
				sequence.m_Request->m_Sequence.m_OutputDirectory == std::filesystem::path("D:/captures") &&
				sequence.m_Request->m_Sequence.m_Label == "dolly" && sequence.m_Request->m_Sequence.m_Note == "baseline" &&
				sequence.m_Request->m_Sequence.m_Exports[0].m_Frame == 1 && sequence.m_Request->m_Sequence.m_Exports[0].m_IncludeGraph,
				"Sequence business fields and complete export groups remain available during migration");
			const auto reference = ParseApplicationControlRequest(
				R"({"protocol":3,"id":13,"command":"sequence","path":"SEQ_A","source":"diagnostic","diagnosticTap":"temporal-rejection","referenceSamples":64,"referenceTextureLodBias":-3})");
			context.Check(reference.m_Request && reference.m_Request->m_Sequence.m_CaptureSource == FrameCaptureSource::Diagnostic &&
				reference.m_Request->m_Sequence.m_DiagnosticTap == PostProcessDebugTap::TemporalRejection &&
				reference.m_Request->m_Sequence.m_ReferenceSamples == 64 && reference.m_Request->m_Sequence.m_ReferenceTextureLodBias == -3.0f,
				"Sequence diagnostic and supersampled reference controls remain intact");
			const auto evaluation = ParseApplicationControlRequest(
				R"({"protocol":3,"id":15,"command":"sequence","path":"SEQ_A","gpuTiming":true,"temporalAA":{"neighborhoodClampExpansion":1,"maxHistoryFeedback":0.9,"historyFilter":"bilinear","currentFilter":"point","motionSelection":"center","postTemporalView":"jittered","resolutionPreset":"quality","historyRectification":"variance-clip","varianceClipGamma":1.25,"historyAccumulation":"compatibility-age","historyRelaxation":2,"textureLodBiasOffset":-1.5,"enabled":false},"gtao":{"temporal":true,"temporalMaxSamples":24,"directionCount":1,"stepCount":8}})");
			const auto* overrides = evaluation.m_Request ? &evaluation.m_Request->m_Sequence.m_TemporalAAOverrides : nullptr;
			context.Check(overrides && evaluation.m_Request->m_Sequence.m_GpuTiming &&
				overrides->m_NeighborhoodClampExpansion == 1.0f && overrides->m_MaxHistoryFeedback == 0.9f &&
				!overrides->m_VelocityWeightScale && overrides->m_HistoryFilter == TemporalAAHistoryFilter::Bilinear &&
				overrides->m_CurrentFilter == TemporalAACurrentFilter::Point && overrides->m_MotionSelection == TemporalAAMotionSelection::Center &&
				overrides->m_PostTemporalView == TemporalAAPostTemporalView::Jittered && overrides->m_ResolutionPreset == TemporalAAResolutionPreset::Quality &&
				overrides->m_HistoryRectification == TemporalAAHistoryRectification::VarianceClip && overrides->m_VarianceClipGamma == 1.25f &&
				overrides->m_HistoryAccumulation == TemporalAAHistoryAccumulation::CompatibilityAge && overrides->m_HistoryRelaxation == 2.0f &&
				overrides->m_TextureLodBiasOffset == -1.5f && overrides->m_Enabled == false,
				"Sequence Temporal AA overrides and GPU timing preserve exact values");
			context.Check(evaluation.m_Request && evaluation.m_Request->m_Sequence.m_GTAOOverrides.m_TemporalAccumulation == true &&
				evaluation.m_Request->m_Sequence.m_GTAOOverrides.m_TemporalMaxSamples == 24u &&
				evaluation.m_Request->m_Sequence.m_GTAOOverrides.m_DirectionCount == 1u && evaluation.m_Request->m_Sequence.m_GTAOOverrides.m_StepCount == 8u,
				"Sequence GTAO overrides preserve exact values");
			context.Check(ParseApplicationControlRequest(R"({"protocol":3,"id":12,"command":"sequence-cancel"})").m_Request.has_value(),
				"Sequence cancellation remains a distinct business command");
			const auto watch = ParseApplicationControlRequest(
				R"({"protocol":3,"id":70,"command":"watch","mode":"report","claims":["boundary"],"targets":[{"resource":"Motion","at":{"before":"TAA"},"occurrence":2,"slices":[2,1]}]})");
			const auto buffer = ParseApplicationControlRequest(
				R"({"protocol":3,"id":71,"command":"watch","targets":[{"resource":"Values","buffer":{"stride":16,"validElementCount":3,"fields":[{"offset":0,"type":"uint32"},{"offset":4,"type":"float32","count":2},{"offset":12,"type":"float16","count":2}]}}]})");
			const auto off = ParseApplicationControlRequest(R"({"protocol":3,"id":72,"command":"watch","enabled":false})");
			const auto production = ParseApplicationControlRequest(R"({"protocol":3,"id":73,"command":"watch","claims":["production"]})");
			context.Check(watch.m_Request && !watch.m_Request->m_Watch.m_Production &&
				watch.m_Request->m_Watch.m_Targets[0].m_Address.m_Occurrence == 2u && buffer.m_Request &&
				buffer.m_Request->m_Watch.m_Targets[0].m_BufferLayout.m_Components.size() == 4 &&
				buffer.m_Request->m_Watch.m_Targets[0].m_BufferLayout.m_ScalarCount == 12 && off.m_Request &&
				!off.m_Request->m_Watch.m_Enabled && production.m_Request && !production.m_Request->m_Watch.m_Boundary && production.m_Request->m_Watch.m_Production,
				"Watch preserves typed addresses, valid floating buffer fields, disabled and production-only modes");
			const auto injection = ParseApplicationControlRequest(
				R"({"protocol":3,"id":74,"command":"watch","mode":"fail","inject":{"point":"pass-temporal-aa.csmain.history","value":"NegativeInfinity","x":12,"y":34,"component":3}})");
#if GGLAB_DEVELOPMENT_BUILD
			context.Check(injection.m_Request && injection.m_Request->m_Watch.m_Injection &&
				injection.m_Request->m_Watch.m_Injection->m_Value == NonFiniteInjectionValue::NegativeInfinity &&
				injection.m_Request->m_Watch.m_Injection->m_X == 12 && injection.m_Request->m_Watch.m_Injection->m_Y == 34 &&
				injection.m_Request->m_Watch.m_Injection->m_Component == 3,
				"Development watch accepts a registered injection with its exact location");
#else
			context.Check(!injection.m_Request, "Release rejects non-finite injection");
#endif
			// Every malformed fixture is checked independently, rather than masking
			// failures behind a single aggregate check or an obsolete protocol version.
			const std::string_view rejected[] =
			{
				R"({"protocol":1,"id":3,"command":"status"})",
				R"({"protocol":2,"id":3,"command":"capture"})",
				R"({"protocol":4,"id":3,"command":"status"})",
				R"({"protocol":3,"id":75,"command":"watch","inject":{"point":"unknown.point","value":"NaN","x":0,"y":0}})",
				R"({"protocol":3,"id":76,"command":"watch","inject":{"point":"pass-temporal-aa.csmain.history","value":"nan","x":0,"y":0}})",
				R"({"protocol":3,"id":77,"command":"watch","inject":{"point":"pass-temporal-aa.csmain.history","value":"NaN","x":0,"y":0,"component":4}})",
				R"({"protocol":3,"id":78,"command":"watch","inject":{"point":"pass-temporal-aa.csmain.history","value":"NaN","x":0}})",
				R"({"protocol":3,"id":80,"command":"watch","claims":[]})",
				R"({"protocol":3,"id":81,"command":"watch","claims":["boundary","boundary"]})",
				R"({"protocol":3,"id":82,"command":"watch","claims":["other"]})",
				R"({"protocol":3,"id":83,"command":"watch","mode":"fatal"})",
				R"({"protocol":3,"id":84,"command":"watch","enabled":1})",
				R"({"protocol":3,"id":85,"command":"watch","enabled":false,"targets":[]})",
				R"({"protocol":3,"id":86,"command":"watch","target":[]})",
				R"({"protocol":3,"id":87,"command":"watch","targets":[null]})",
				R"({"protocol":3,"id":88,"command":"watch","targets":[{"resource":"A","buffer":{}}]})",
				R"({"protocol":3,"id":89,"command":"watch","targets":[{"resource":"A","buffer":{"stride":4,"validElementCount":2,"fields":[{"offset":0,"type":"uint32"}]}}]})",
				R"({"protocol":3,"id":90,"command":"watch","targets":[{"resource":"A","buffer":{"stride":4,"validElementCount":-1,"fields":[{"offset":0,"type":"float32"}]}}]})",
				R"({"protocol":3,"id":91,"command":"watch","targets":[{"resource":"A","buffer":{"stride":4,"validElementCount":1,"fields":[{"offset":0,"type":"float32","count":4294967295}]}}]})",
				R"({"protocol":3,"id":92,"command":"watch","targets":[{"resource":"A","buffer":{"stride":8,"validElementCount":1,"fields":[{"offset":0,"type":"float32","count":2},{"offset":4,"type":"float32"}]}}]})",
				R"({"protocol":3,"id":93,"command":"watch","targets":[{"resource":"A","buffer":{"stride":4,"validElementCount":1,"fields":[{"offset":0,"type":"float32","extra":1}]}}]})",
				R"({"protocol":3,"id":62,"command":"export","targets":[]})",
				R"({"protocol":3,"id":63,"command":"export","targets":[{"resource":"A","at":{"before":"P","after":"P"}}]})",
				R"({"protocol":3,"id":64,"command":"export","targets":[{"resource":"A","occurrence":0}]})",
				R"({"protocol":3,"id":65,"command":"export","targets":[{"resource":"A","slices":[0,0]}]})",
				R"({"protocol":3,"id":66,"command":"export","targets":[{"resource":"A","mipp":1}]})",
				R"({"protocol":3,"id":67,"command":"export","targets":[{"resource":"A","aspect":"all"}]})",
				R"({"protocol":3,"id":68,"command":"export","targets":[{"resource":"A"}],"graph":1})",
				R"({"protocol":3,"command":"status"})",
				R"({"protocol":3,"id":4,"command":"reboot"})",
				R"({"protocol":3,"id":5,"command":"status","extra":1})",
				R"({"protocol":3,"id":6,"command":"capture","source":"hud"})",
				R"({"protocol":3,"id":7,"command":"capture","settleFrames":-1})",
				R"({"protocol":3,"id":8,"command":"capture","outputDirectory":"relative"})",
				R"({"protocol":3,"id":9,"command":"capture","lable":"typo"})",
				R"({"protocol":3,"id":10,"command":"result"})",
				R"({"protocol":3,"id":11,"command":"result","jobId":0})",
				R"({"protocol":3,"id":12,"command":"sequence"})",
				R"({"protocol":3,"id":13,"command":"sequence","path":"A","captureFrames":[-1]})",
				R"({"protocol":3,"id":14,"command":"sequence","path":"A","captureFrames":3})",
				R"({"protocol":3,"id":15,"command":"sequence","path":"A","frames":[1]})",
				R"({"protocol":3,"id":16,"command":"sequence-cancel","path":"A"})",
				R"({"protocol":3,"id":17,"command":"capture","source":"diagnostic"})",
				R"({"protocol":3,"id":18,"command":"capture","diagnosticTap":"temporal-rejection"})",
				R"({"protocol":3,"id":19,"command":"sequence","path":"A","source":"diagnostic","diagnosticTap":"bloom-result"})",
				R"({"protocol":3,"id":20,"command":"sequence","path":"A","referenceSamples":5000})",
				R"({"protocol":3,"id":21,"command":"sequence","path":"A","temporalAA":{"maxHistoryFeedback":1.5}})",
				R"({"protocol":3,"id":22,"command":"sequence","path":"A","temporalAA":{"historyFilter":1}})",
				R"({"protocol":3,"id":23,"command":"sequence","path":"A","temporalAA":{"velocityWeightScale":"x"}})",
				R"({"protocol":3,"id":24,"command":"sequence","path":"A","temporalAA":[]})",
				R"({"protocol":3,"id":25,"command":"sequence","path":"A","gpuTiming":1})",
				R"({"protocol":3,"id":26,"command":"sequence","path":"A","temporalAA":{"historyFilter":"bicubic"}})",
				R"({"protocol":3,"id":27,"command":"sequence","path":"A","temporalAA":{"historyFilter":"catmull-rom"}})",
				R"({"protocol":3,"id":28,"command":"sequence","path":"A","temporalAA":{"currentFilter":"box"}})",
				R"({"protocol":3,"id":29,"command":"sequence","path":"A","temporalAA":{"currentFilter":"gaussian-narrow"}})",
				R"({"protocol":3,"id":30,"command":"sequence","path":"A","temporalAA":{"motionSelection":"dilated"}})",
				R"({"protocol":3,"id":31,"command":"sequence","path":"A","temporalAA":{"motionSelection":"closest-depth-correspondence"}})",
				R"({"protocol":3,"id":32,"command":"sequence","path":"A","temporalAA":{"textureLodBiasOffset":-3}})",
				R"({"protocol":3,"id":33,"command":"sequence","path":"A","referenceTextureLodBias":"low"})",
				R"({"protocol":3,"id":34,"command":"sequence","path":"A","temporalAA":{"enabled":0}})",
				R"({"protocol":3,"id":35,"command":"sequence","path":"A","temporalAA":{"postTemporalView":"none"}})",
				R"({"protocol":3,"id":36,"command":"sequence","path":"A","temporalAA":{"resolutionPreset":"balanced"}})",
				R"({"protocol":3,"id":37,"command":"sequence","path":"A","temporalAA":{"historyRectification":"variance"}})",
				R"({"protocol":3,"id":38,"command":"sequence","path":"A","temporalAA":{"varianceClipGamma":8}})",
				R"({"protocol":3,"id":39,"command":"sequence","path":"A","temporalAA":{"historyAccumulation":"age"}})",
				R"({"protocol":3,"id":40,"command":"sequence","path":"A","temporalAA":{"historyRelaxation":5}})",
				R"({"protocol":3,"id":41,"command":"sequence","path":"A","gtao":{"temporal":1}})",
				R"({"protocol":3,"id":42,"command":"sequence","path":"A","gtao":{"temporalMaxSamples":0}})",
				R"({"protocol":3,"id":43,"command":"sequence","path":"A","gtao":{"temporalMaxSamples":2.5}})",
				R"({"protocol":3,"id":44,"command":"sequence","path":"A","gtao":{"radius":2}})",
				R"({"protocol":3,"id":45,"command":"sequence","path":"A","gtao":{"directionCount":9}})",
				R"({"protocol":3,"id":51,"command":"graph","summary":1})",
				R"({"protocol":3,"id":52,"command":"graph","source":"scene"})",
				R"({"protocol":3,"id":53,"command":"graph","extra":true})",
				R"({"protocol":3,"id":54,"command":"capture","summary":true})",
				R"({"protocol":3,"id":101,"command":"capture","wait":false})",
				R"({"protocol":3,"id":102,"command":"result","requestId":1})",
				R"({"protocol":3,"id":103,"command":"result","jobId":1,"consume":1})",
				R"({"protocol":3,"id":104,"command":"capture-cancel","jobId":1,"consume":true})",
				R"({"protocol":3,"id":105,"command":"result","jobId":1,"extra":1})",
				R"({"protocol":3,"id":106,"command":"capture","products":[]})",
				R"({"protocol":3,"id":107,"command":"capture","products":[{"kind":"image","source":"diagnostic"}]})",
				R"({"protocol":3,"id":108,"command":"capture","products":[{"kind":"image","source":"scene","optional":true}]})",
				R"({"protocol":3,"id":109,"command":"capture","products":[{"kind":"diagnostic"}]})",
				R"({"protocol":3,"id":110,"command":"capture","products":[{"kind":"graph","summary":1}]})",
				R"({"protocol":3,"id":111,"command":"capture","products":[{"kind":"resource","targets":[]}]})",
				R"({"protocol":3,"id":112,"command":"capture","products":[{"kind":"other"}]})",
				R"({"protocol":3,"id":113,"command":"capture","products":[{"kind":"graph"}],"source":"scene"})",
				R"({"protocol":3,"id":114,"command":"graph","products":[{"kind":"graph"}]})",
				"not json", "[1,2]",
			};
			for (const auto line : rejected)
			{
				const auto parsed = ParseApplicationControlRequest(line);
				const auto document = Json::parse(line, nullptr, false);
				const uint64_t id = document.is_object() ? document.value("id", uint64_t{ 0 }) : 0;
				context.Check(!parsed.m_Request && !parsed.m_Error.empty() && parsed.m_Id == id,
					std::format("Malformed, retired or unsupported request is rejected with its id: {}", line));
			}
			Json oversized = { { "protocol", 3 }, { "id", 115 }, { "command", "capture" }, { "products", Json::array() } };
			for (uint32_t index = 0; index < 9; ++index)
			{
				oversized["products"].push_back({ { "kind", "image" }, { "source", "scene" } });
			}
			context.Check(!ParseApplicationControlRequest(oversized.dump()).m_Request, "Product array is bounded before admission");
		}

		void RunSerializeTests(SelfTestContext& context) noexcept
		{
			const auto error = Json::parse(SerializeApplicationControlError(3, "bad \"input\""));
			context.Check(error["protocol"] == 3 && error["id"] == 3 && error["ok"] == false && error["error"] == "bad \"input\"",
				"Errors carry v3, correlation and correctly escaped text");
			NonFiniteWatchStatus watch{ .m_ConfigurationVersion = 4, .m_Enabled = true,
				.m_BoundaryRequested = true, .m_ProductionRequested = false, .m_BoundaryFrameSerial = 123,
				.m_Boundary = { NonFiniteClaimStatus::Failed, NonFiniteWatchFailure::NonFiniteDetected } };
			NonFiniteWatchTargetStatus target{ .m_Address = { .m_Resource = "Motion", .m_Mip = 1 },
				.m_Record = { .m_CheckedCount = 8, .m_NanCount = 1 },
				.m_Result = { .m_Claim = watch.m_Boundary, .m_FirstNonFiniteIndex = 0 },
				.m_TextureLocation = NonFiniteTextureLocation{ 0, 0, 2, 0 } };
			watch.m_Targets.push_back(target);
			watch.m_FirstAnomaly = { .m_FrameSerial = 123, .m_Failure = NonFiniteWatchFailure::NonFiniteDetected, .m_Target = target };
			const auto report = Json::parse(SerializeApplicationControlWatch(4, watch, false))["watch"]["report"];
			context.Check(report["configurationVersion"] == 4 && report["boundaryFrameSerial"] == 123 &&
				report["coverage"] == "latest-completed-frame" && report["targets"][0]["firstIndex"] == 0 &&
				report["targets"][0]["location"]["slice"] == 2 && report["firstAnomaly"]["reason"] == "NonFiniteDetected" &&
				Json::parse(SerializeApplicationControlStatus(5, { .m_Watch = &watch }))["watch"]["report"] == report,
				"Configuration and status share exact watch coverage, separate frame identities and first anomaly location");
			watch.m_DefaultTargets = true;
			watch.m_Targets = { NonFiniteWatchTargetStatus{ .m_Semantic = NonFiniteWatchSemantic::AmbientOcclusion,
				.m_Result = { .m_Claim = { NonFiniteClaimStatus::NotApplicable, NonFiniteWatchFailure::None } } } };
			watch.m_FirstAnomaly.reset();
			const auto defaults = Json::parse(SerializeApplicationControlWatch(6, watch, false))["watch"]["report"];
			context.Check(defaults["targetsMode"] == "defaults" && defaults["defaultTiers"] == Json::array({ "A", "B" }) &&
				defaults["targets"][0]["semantic"] == "ambient-occlusion" && defaults["targets"][0]["address"].is_null() &&
				defaults["targets"][0]["claim"] == "NotApplicable", "Inactive watch defaults never invent a resource address");
			watch.m_Mode = NonFiniteWatchMode::Fail;
			watch.m_Targets = { NonFiniteWatchTargetStatus{ .m_Semantic = NonFiniteWatchSemantic::Specular,
				.m_Address = { .m_Resource = "IBL.Specular", .m_Mip = 4, .m_SliceCount = 6 },
				.m_Generation = { { 12, 3 }, 9 }, .m_CheckedFrameSerial = 77 } };
			const auto generation = Json::parse(SerializeApplicationControlWatch(7, watch, false))["watch"]["report"];
			context.Check(generation["mode"] == "fail" && generation["targets"][0]["semantic"] == "ibl.specular" &&
				generation["targets"][0]["watchTier"] == 2 && generation["targets"][0]["lastCheckedFrameSerial"] == 77 &&
				generation["targets"][0]["resourceIdentity"] == Json({ { "generation", 3 }, { "index", 12 } }),
				"Watch retains fail mode, content generation, physical identity, tier and checked frame");
			FrameCaptureFrameState frame{ .m_Backend = "vulkan", .m_DemoId = "Demo.LabHost", .m_LabId = "gglab.lab.culling",
				.m_FrameIndex = 42, .m_ReferenceViewIds = { "CAM_A", "CAM_B" } };
			frame.m_Readiness.Add("lab", FrameCaptureGateState::Pending, "Warming up.");
			const auto status = Json::parse(SerializeApplicationControlStatus(4, {
				.m_SessionId = "s1", .m_ProcessId = 99, .m_Hidden = true, .m_Width = 640, .m_Height = 360,
				.m_CaptureIntake = { .m_ActiveJobs = 2, .m_ReservedReceipts = 3 }, .m_SettledFrames = 5, .m_Frame = &frame }));
			context.Check(status["session"]["pid"] == 99 && status["frame"]["labId"] == "gglab.lab.culling" &&
				status["frame"]["ready"] == false && status["frame"]["settledFrames"] == 5 &&
				status["frame"]["gates"][0]["state"] == "pending" && status["frame"]["fixedDeltaTime"].is_null() &&
				status["frame"]["referenceViews"] == Json::array({ "CAM_A", "CAM_B" }), "Status preserves session and frame readiness");
			context.Check(!status["capture"].contains("legacyUnfinishedOutputs") &&
				!status["capture"].contains("legacyRetainedResults"),
				"Status exposes only current job ownership without retired output counters");
			context.Check(status["capabilities"]["captureJobs"] == true && status["capabilities"]["sequenceCaptureJobs"] == true &&
				status["capabilities"]["resultConsumption"] == "explicit" && status["capture"]["activeJobs"] == 2 &&
				status["capture"]["reservedReceipts"] == 3 && status["capture"]["limits"]["receipts"] == 128 &&
				status["capture"]["limits"]["activeJobs"] == 64 && status["capture"]["limits"]["productsPerJob"] == 8 &&
				status["capture"]["limits"]["resourceTargetsPerJob"] == 64, "Capabilities advertise actual count policy for the final bounded job contract");
			const auto noFrame = Json::parse(SerializeApplicationControlStatus(5, {}));
			const auto available = Json::parse(SerializeApplicationControlStatus(6, { .m_NonFiniteInstrumentation = true }));
			context.Check(noFrame["frame"].is_null() && noFrame["sequence"].is_null() && noFrame["watch"]["production"] == "Unavailable" &&
				available["watch"]["production"] == "Incomplete", "Missing frame and production capability do not imply completed coverage");
			const std::string environment = SerializeCaptureEnvironment({}, "0123456789abcdef0123456789abcdef");
			const auto withEnvironment = Json::parse(SerializeApplicationControlStatus(7, { .m_Environment = environment }));
			context.Check(noFrame["environment"].is_null() && withEnvironment["environment"] == Json::parse(environment) &&
				Json::parse(SerializeApplicationControlStatus(8, { .m_Environment = "not json" }))["environment"].is_null() &&
				withEnvironment["capabilities"]["bundleSchemaVersion"] == CaptureBundleSchemaVersion,
				"Status carries the runtime's environment record unchanged, or null before the runtime has one");
			FrameSequenceStatus sequence{ .m_SequenceId = 3, .m_State = FrameSequenceState::Failed,
				.m_CameraPathId = "SEQ_DollyDoorway", .m_CameraPathVersion = 1, .m_FrameCount = 180,
				.m_SubmittedFrames = 12, .m_AcceptedCaptureJobs = 2, .m_FinishedCaptureJobs = 1,
				.m_PendingCaptureJobs = 1, .m_CompletedCaptures = 1, .m_Failure = "Lost continuity." };
			const auto sequenceJson = Json::parse(SerializeApplicationControlSequence(10, sequence))["sequence"];
			context.Check(sequenceJson["state"] == "failed" && sequenceJson["path"] == "SEQ_DollyDoorway" &&
				sequenceJson["frameCount"] == 180 && sequenceJson["submittedFrames"] == 12 && sequenceJson["failure"] == "Lost continuity." &&
				!sequenceJson.contains("captureRequestIds") && !sequenceJson.contains("gpuTiming") &&
				sequenceJson["acceptedCaptureJobs"] == 2 && sequenceJson["finishedCaptureJobs"] == 1 &&
				sequenceJson["pendingCaptureJobs"] == 1 &&
				Json::parse(SerializeApplicationControlStatus(11, { .m_Sequence = &sequence }))["sequence"] == sequenceJson,
				"Sequence progress remains available without exposing legacy output ids as job identities");
			sequence.m_GpuTiming = FrameSequenceGpuTiming{ .m_FrameMilliseconds = { 4.0, 2.0 },
				.m_Scopes = { { .m_Name = "PostProcess.TemporalAA", .m_Milliseconds = { 0.25 } } } };
			const auto timing = Json::parse(SerializeApplicationControlSequence(12, sequence))["sequence"]["gpuTiming"];
			context.Check(timing["frames"] == 2 && timing["frame"]["medianMs"] == 2.0 &&
				timing["scopes"][0]["name"] == "PostProcess.TemporalAA" && timing["scopes"][0]["p90Ms"] == 0.25,
				"Sequence GPU timing retains per-scope summaries");
			const auto accepted = Json::parse(SerializeApplicationControlCaptureAdmission(8, { CaptureAdmissionStatus::Accepted, CaptureAdmissionReason::None, 12 }));
			context.Check(accepted["ok"] == true && accepted["admission"] == "Accepted" && accepted["jobId"] == 12,
				"Accepted admission exposes one job identity");
			for (const auto reason : { CaptureAdmissionReason::ActiveJobLimit, CaptureAdmissionReason::ReceiptLimit,
				CaptureAdmissionReason::InvalidPlan, CaptureAdmissionReason::PlanTooLarge, CaptureAdmissionReason::ShuttingDown,
				CaptureAdmissionReason::IdentityExhausted, CaptureAdmissionReason::IdentityUnavailable, CaptureAdmissionReason::ServicesUnavailable })
			{
				const bool busy = reason == CaptureAdmissionReason::ActiveJobLimit || reason == CaptureAdmissionReason::ReceiptLimit;
				const auto rejected = Json::parse(SerializeApplicationControlCaptureAdmission(9,
					{ busy ? CaptureAdmissionStatus::Busy : CaptureAdmissionStatus::Rejected, reason, 0 }));
				context.Check(rejected["ok"] == false && rejected["jobId"].is_null() && rejected["reason"] != "None" &&
					rejected["admission"] == (busy ? "Busy" : "Rejected"), "Unaccepted admission never exposes a usable identity");
			}
			const auto pending = Json::parse(SerializeApplicationControlCapturePending(10,
				{ .m_JobId = 12, .m_Stage = CaptureJobStage::AwaitingCoverage, .m_AttemptCount = 2, .m_FrameSerial = 77, .m_RequiredProducts = 2, .m_FinishedProducts = 1 }));
			context.Check(pending["status"] == "pending" && pending["consumed"] == false && pending["stage"] == "AwaitingCoverage" &&
				pending["attemptCount"] == 2 && pending["requiredProducts"] == 2 && pending["finishedProducts"] == 1,
				"Pending result reports progress and never claims consumption");
			CaptureJobResult completed{ .m_JobId = 12, .m_Status = CaptureJobStatus::Completed, .m_Publication = CaptureJobPublication::Published,
				.m_Products = { { .m_ProductIndex = 0, .m_Status = CaptureJobStatus::Completed, .m_Publication = CaptureJobPublication::Published,
					.m_Files = { "D:/captures/a.png", "D:/captures/a.json" }, .m_Metadata = FrameCaptureMetadata{ .m_FrameSerial = 77 } } },
				.m_BundleId = "run-j12-a1", .m_ManifestPath = "D:/captures/a.capture-bundle.json" };
			const auto borrowed = Json::parse(SerializeApplicationControlCaptureResult(11, completed, false));
			auto consumed = Json::parse(SerializeApplicationControlCaptureResult(11, completed, true));
			context.Check(borrowed["status"] == "completed" && borrowed["publication"] == "Published" && borrowed["frameSerial"] == 77 &&
				borrowed["manifest"] == "D:/captures/a.capture-bundle.json" && borrowed["products"][0]["index"] == 0 &&
				borrowed["products"][0]["files"][0] == "D:/captures/a.png" && borrowed["products"][0]["kind"] == "image" && !borrowed.contains("requestId") && !borrowed.contains("image"),
				"Job result exposes manifest and ordered products, not legacy flattened output routing");
			const CaptureJobResult cancelledDiagnostic{ .m_JobId = 13, .m_Status = CaptureJobStatus::Cancelled,
				.m_Products = { { .m_ProductIndex = 0, .m_Kind = CaptureProductKind::Diagnostic,
					.m_Status = CaptureJobStatus::Cancelled } } };
			const auto diagnosticJson = Json::parse(SerializeApplicationControlCaptureResult(12, cancelledDiagnostic, false));
			context.Check(diagnosticJson["products"][0]["kind"] == "diagnostic" &&
				diagnosticJson["products"][0]["metadata"].is_null(),
				"Cancellation before issue retains typed product identity without inventing frame metadata");
			context.Check(consumed["consumed"] == true && borrowed["consumed"] == false, "Terminal receipt consumption is explicit on the wire");
			consumed["consumed"] = false;
			context.Check(consumed == borrowed, "Borrow and consume serialize the identical immutable receipt");
			struct PublicationCase
			{
				CaptureJobPublication m_Publication;
				const char* m_Name;
			};
			const PublicationCase publications[] =
			{
				{ CaptureJobPublication::NotPublished, "NotPublished" },
				{ CaptureJobPublication::PartiallyPublished, "PartiallyPublished" },
				{ CaptureJobPublication::Indeterminate, "Indeterminate" },
			};
			for (const auto& publication : publications)
			{
				completed.m_Status = CaptureJobStatus::Failed;
				completed.m_Publication = publication.m_Publication;
				completed.m_ManifestPath.clear();
				completed.m_Products[0].m_Status = CaptureJobStatus::Failed;
				completed.m_Products[0].m_Failure = "Device lost.";
				const auto failed = Json::parse(SerializeApplicationControlCaptureResult(12, completed, false));
				context.Check(failed["status"] == "failed" && failed["publication"] == publication.m_Name && failed["manifest"].is_null() &&
					failed["products"][0]["failure"] == "Device lost.", "Failure publication remains independent of status and file existence");
			}
			struct CancellationCase
			{
				CaptureCancelResult m_Result;
				const char* m_Name;
			};
			const CancellationCase cancellations[] =
			{
				{ CaptureCancelResult::Cancelled, "Cancelled" },
				{ CaptureCancelResult::TooLate, "TooLate" },
				{ CaptureCancelResult::Finished, "Finished" },
				{ CaptureCancelResult::NotFound, "NotFound" },
			};
			for (const auto& cancellation : cancellations)
			{
				const auto cancelled = Json::parse(SerializeApplicationControlCaptureCancellation(
					13, 12, cancellation.m_Result));
				context.Check(cancelled["ok"] == true && cancelled["jobId"] == 12 &&
					cancelled["cancellation"] == cancellation.m_Name &&
					!cancelled.contains("status") && !cancelled.contains("publication"),
					"Cancellation outcome does not overwrite actual job outcome");
			}
			context.Check(Json::parse(SerializeApplicationControlCaptureNotFound(14, 12))["status"] == "not-found" &&
				Json::parse(SerializeApplicationControlStopping(15))["status"] == "stopping", "NotFound and stop acknowledgements are distinct");
		}

		// AppRuntime-like receipt store shared by the session and every other host owner.
		struct SharedCaptureJobs
		{
			uint64_t m_NextJobId = 1;
			bool m_Busy = false;
			std::vector<uint64_t> m_Active;
			std::vector<CaptureJobResult> m_Published;
			std::vector<uint64_t> m_Cancelled;

			CaptureAdmission Submit() noexcept
			{
				if (m_Busy)
				{
					return { CaptureAdmissionStatus::Busy, CaptureAdmissionReason::ReceiptLimit };
				}
				m_Active.push_back(m_NextJobId);
				return { CaptureAdmissionStatus::Accepted, CaptureAdmissionReason::None, m_NextJobId++ };
			}
			void Finish(uint64_t jobId, CaptureJobStatus status = CaptureJobStatus::Completed) noexcept
			{
				if (std::erase(m_Active, jobId) != 0)
				{
					m_Published.push_back({ .m_JobId = jobId, .m_Status = status,
						.m_Publication = status == CaptureJobStatus::Completed ?
							CaptureJobPublication::Published :
							CaptureJobPublication::NotPublished });
				}
			}
			[[nodiscard]] const CaptureJobResult* Find(uint64_t jobId) const noexcept
			{
				const auto result = std::ranges::find(m_Published, jobId, &CaptureJobResult::m_JobId);
				return result != m_Published.end() ? &*result : nullptr;
			}
			[[nodiscard]] std::optional<CaptureJobResult> Take(uint64_t jobId) noexcept
			{
				const auto result = std::ranges::find(m_Published, jobId, &CaptureJobResult::m_JobId);
				if (result == m_Published.end())
				{
					return std::nullopt;
				}
				CaptureJobResult taken = std::move(*result);
				m_Published.erase(result);
				return taken;
			}
			[[nodiscard]] std::optional<CaptureJobSnapshot> GetSnapshot(uint64_t jobId) const noexcept
			{
				const bool active = std::ranges::find(m_Active, jobId) != m_Active.end();
				if (!active && !Find(jobId))
				{
					return std::nullopt;
				}
				return CaptureJobSnapshot{ .m_JobId = jobId,
					.m_Stage = active ? CaptureJobStage::AwaitingProducts : CaptureJobStage::Finished };
			}
			CaptureCancelResult Cancel(uint64_t jobId) noexcept
			{
				if (std::ranges::find(m_Active, jobId) == m_Active.end())
				{
					return Find(jobId) ? CaptureCancelResult::Finished : CaptureCancelResult::NotFound;
				}
				m_Cancelled.push_back(jobId);
				Finish(jobId, CaptureJobStatus::Cancelled);
				return CaptureCancelResult::Cancelled;
			}
		};

		void RunCaptureJobOwnershipTests(SelfTestContext& context) noexcept
		{
			SharedCaptureJobs jobs;
			ApplicationControlCaptureJobs session({
				.m_TrySubmit = [&](CapturePlan&&) { return jobs.Submit(); },
				.m_Cancel = [&](uint64_t jobId) { return jobs.Cancel(jobId); },
				.m_FindResult = [&](uint64_t jobId) { return jobs.Find(jobId); },
				.m_TakeResult = [&](uint64_t jobId) { return jobs.Take(jobId); },
				.m_GetSnapshot = [&](uint64_t jobId) { return jobs.GetSnapshot(jobId); },
			});
			// Interactive, startup and sequence jobs share the job counter but not ownership.
			const uint64_t foreign = jobs.Submit().m_JobId;
			const auto admitted = Json::parse(session.Submit(1, CapturePlan{}));
			const uint64_t owned = admitted["jobId"].get<uint64_t>();
			const auto foreignPending = Json::parse(session.Query(2, foreign, true));
			const auto foreignCancel = Json::parse(session.Cancel(3, foreign));
			context.Check(admitted["ok"] == true && owned != foreign &&
				session.GetOwnedJobCount() == 1 && foreignPending["ok"] == false &&
				foreignPending["status"] == "not-found" &&
				foreignCancel["cancellation"] == "NotFound" && jobs.m_Cancelled.empty() &&
				jobs.GetSnapshot(foreign).has_value(),
				"A session client can neither observe nor cancel another owner's active job");
			const auto ownedPending = Json::parse(session.Query(4, owned, true));
			context.Check(ownedPending["status"] == "pending" && session.GetOwnedJobCount() == 1,
				"A session client observes its own pending job without consuming it");

			jobs.Finish(foreign);
			jobs.Finish(owned);
			const auto foreignTerminal = Json::parse(session.Query(5, foreign, true));
			context.Check(foreignTerminal["status"] == "not-found" && jobs.Find(foreign),
				"A session client cannot consume another owner's terminal receipt");
			const auto borrowed = Json::parse(session.Query(6, owned, false));
			const auto consumed = Json::parse(session.Query(7, owned, true));
			const auto released = Json::parse(session.Query(8, owned, false));
			context.Check(borrowed["status"] == "completed" && borrowed["consumed"] == false &&
				consumed["status"] == "completed" && consumed["consumed"] == true &&
				!jobs.Find(owned) && session.GetOwnedJobCount() == 0 &&
				released["status"] == "not-found",
				"A session client borrows, then consumes its own receipt once and releases the identity");
			context.Check(jobs.Take(foreign).has_value(),
				"The foreign owner still consumes its own receipt");

			jobs.m_Busy = true;
			const auto busy = Json::parse(session.Submit(9, CapturePlan{}));
			jobs.m_Busy = false;
			const auto accepted = Json::parse(session.Submit(10, CapturePlan{}));
			const uint64_t cancelled = accepted["jobId"].get<uint64_t>();
			const auto cancellation = Json::parse(session.Cancel(11, cancelled));
			const uint32_t retained = session.GetOwnedJobCount();
			const auto cancelledResult = Json::parse(session.Query(12, cancelled, true));
			context.Check(busy["ok"] == false && busy["jobId"].is_null() &&
				cancellation["cancellation"] == "Cancelled" &&
				jobs.m_Cancelled == std::vector<uint64_t>{ cancelled } && retained == 1 &&
				cancelledResult["status"] == "cancelled" && session.GetOwnedJobCount() == 0,
				"Busy creates no session identity; cancelling an owned job keeps it until its receipt is consumed");
		}

		[[nodiscard]] std::optional<std::string> SendPipeLine(const std::wstring& pipeName, std::string_view line) noexcept
		{
			HANDLE pipe = INVALID_HANDLE_VALUE;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (pipe == INVALID_HANDLE_VALUE && std::chrono::steady_clock::now() < deadline)
			{
				pipe = ::CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
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
			if (::WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()), &bytes, nullptr))
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
				::GetCurrentProcessId(), std::chrono::steady_clock::now().time_since_epoch().count());
			win32::NamedPipeServer server;
			win32::NamedPipeServer duplicate;
			const bool started = server.Start(pipeName, std::chrono::milliseconds(300));
			context.Check(started && !duplicate.Start(pipeName), "A control pipe name can be served by only one server");
			if (!started)
			{
				return;
			}
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
			context.Check(requests.size() == 2 && firstResponse == "echo:first" && secondResponse == "echo:second",
				"Concurrent clients each receive exactly the first response to their line");
			std::optional<std::string> pendingResponse;
			std::thread pending([&]() { pendingResponse = SendPipeLine(pipeName, "pending"); });
			bool received = false;
			const auto pendingDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!received && std::chrono::steady_clock::now() < pendingDeadline)
			{
				received = !server.Poll().empty();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			HANDLE silent = ::CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
			bool silentAnswered = false;
			if (silent != INVALID_HANDLE_VALUE)
			{
				constexpr std::string_view silentRequest = "silent\n";
				DWORD bytes = 0;
				::WriteFile(silent, silentRequest.data(), static_cast<DWORD>(silentRequest.size()), &bytes, nullptr);
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
			context.Check(received && pendingResponse == "stopped", "Stopping answers outstanding requests with the fallback response");
			context.Check(silentAnswered && stopTime < std::chrono::seconds(3), "A client that never reads its response cannot hold server shutdown");
		}
	}

	void RunApplicationControlSelfTests(SelfTestContext& context) noexcept
	{
		RunParseTests(context);
		RunSerializeTests(context);
		RunCaptureJobOwnershipTests(context);
		RunPipeServerTests(context);
	}
}
