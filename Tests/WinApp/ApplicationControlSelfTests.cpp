#include "ApplicationControlSelfTests.h"

#include "Application/Control/ApplicationControlCaptureJobs.h"
#include "Application/Control/ApplicationControlProtocol.h"
#include "Application/Control/ApplicationControlParameters.h"
#include "Application/Platform/Windows/Win32NamedPipeServer.h"
#include "Capture/CaptureBundle.h"
#include "Capture/CaptureEnvironment.h"
#include "Parameters/HostViewParameters.h"
#include "Parameters/ParameterEffectReceipt.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <initializer_list>
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

		void RunParameterControlTests(SelfTestContext& context) noexcept
		{
			const auto parameters = ParseApplicationControlRequest(R"({"protocol":4,"id":700,"command":"parameters"})");
			context.Check(parameters.m_Request && parameters.m_Request->m_Command == ApplicationControlCommand::Parameters,
				"Parameter discovery is an additive v4 command");
			const auto changes = ParseApplicationControlRequest(
				R"({"protocol":4,"id":701,"command":"set","changes":[{"id":"b","type":"bool","value":false},{"id":"i","type":"int","value":-2147483648},{"id":"u","type":"uint","value":4294967295},{"id":"f","type":"float","value":2},{"id":"e","type":"enum","value":2},{"id":"v","type":"vector3","value":[1,2,3]},{"id":"c","type":"color","value":[0,0.5,1,1]}]})");
			const auto* parsed = changes.m_Request ? &changes.m_Request->m_ParameterChanges : nullptr;
			context.Check(parsed && parsed->size() == 7 && std::get<bool>((*parsed)[0].m_Value.m_Value) == false &&
				std::get<int32_t>((*parsed)[1].m_Value.m_Value) == INT32_MIN &&
				std::get<uint32_t>((*parsed)[2].m_Value.m_Value) == UINT32_MAX &&
				std::get<float>((*parsed)[3].m_Value.m_Value) == 2.0f && (*parsed)[4].m_Type == LabParameterType::Enum &&
				std::get<Vector3>((*parsed)[5].m_Value.m_Value).m_Z == 3.0f &&
				std::get<Color>((*parsed)[6].m_Value.m_Value).G() == 0.5f,
				"All seven wire types retain strict owned 32-bit Lab values without string coercion");
			const std::vector<std::string> invalidChanges =
			{
				R"([])", R"({})", R"([{"id":"x","type":"bool","value":1}])",
				R"([{"id":"x","type":"bool","value":"true"}])",
				R"([{"id":"x","type":"int","value":2147483648}])",
				R"([{"id":"x","type":"int","value":1.0}])",
				R"([{"id":"x","type":"int","value":18446744073709551615}])",
				R"([{"id":"x","type":"uint","value":-1}])",
				R"([{"id":"x","type":"uint","value":4294967296}])",
				R"([{"id":"x","type":"float","value":1e100}])",
				R"([{"id":"x","type":"float","value":null}])",
				R"([{"id":"x","type":"vector3","value":[1,2]}])",
				R"([{"id":"x","type":"color","value":[1,2,3,"4"]}])",
				R"([{"id":"x","type":"vector3","value":[1,2,1e100]}])",
				R"([{"id":"x","type":"float","value":1},{"id":"x","type":"float","value":2}])",
				R"([{"id":"","type":"float","value":1}])",
				R"([{"id":"x","type":"unknown","value":1}])",
				R"([{"id":"x","type":"float","value":1,"clamp":true}])",
				R"([{"id":"x","type":"enum","value":"named"}])",
			};
			for (const auto& invalid : invalidChanges)
			{
				const auto rejected = ParseApplicationControlRequest(
					R"({"protocol":4,"id":702,"command":"set","changes":)" + invalid + "}");
				context.Check(!rejected.m_Request && rejected.m_Id == 702 && !rejected.m_Error.empty(),
					"Malformed typed batches reject completely and retain correlation");
			}
			Json oversized = Json::array();
			for (size_t i = 0; i <= ApplicationControlMaxParameterItems; ++i)
			{
				oversized.push_back({ { "id", std::to_string(i) }, { "type", "bool" }, { "value", true } });
			}
			context.Check(!ParseApplicationControlRequest(Json({ { "protocol", 4 }, { "id", 703 },
				{ "command", "set" }, { "changes", oversized } }).dump()).m_Request,
				"Session batches have a fixed admission item bound");
			for (const auto& invalid : { R"({"jobId":1})", R"({"batchId":0})",
				R"({"batchId":1,"consume":1})", R"({"batchId":1,"jobId":1})" })
			{
				Json request = Json::parse(invalid);
				request.update({ { "protocol", 4 }, { "id", 704 }, { "command", "parameter-result" } });
				context.Check(!ParseApplicationControlRequest(request.dump()).m_Request,
					"Parameter results cannot reinterpret capture job identities or consumption types");
			}
			const auto query = ParseApplicationControlRequest(
				R"({"protocol":4,"id":705,"command":"parameter-result","batchId":9,"consume":true})");
			context.Check(query.m_Request && query.m_Request->m_ParameterBatchId == 9 &&
				query.m_Request->m_JobId == 0 && query.m_Request->m_Consume,
				"Parameter query preserves its independent batch identity");
			context.Check(!Json::parse(SerializeApplicationControlParameters(706, nullptr))["ok"] &&
				!Json::parse(SubmitApplicationControlParameters(707, {}, nullptr))["ok"],
				"A missing parameter source rejects discovery and admission");
			const Json missing = Json::parse(QueryApplicationControlParameters(708, 9, true, nullptr));
			context.Check(!missing["ok"] && missing["batchId"] == 9 && missing["status"] == "not-found",
				"Missing parameter ownership never yields a fabricated receipt");
			ParameterBatchStatus batch{ .m_BatchId = 9, .m_Revision = 7,
				.m_ContentId = "gglab.lab.fixture", .m_State = ParameterBatchState::Finished };
			batch.m_Values.push_back({ LabParameterId("f"), 2.0f });
			auto receipt = ParameterEffectReceipt::Begin(LabParameterId("f"), ParameterEffectClass::Immediate, 7, 20);
			GGLAB_UNUSED(receipt->RecordApplied(ParameterEffectClass::Immediate, { .m_Revision = 7 }, 20));
			GGLAB_UNUSED(receipt->RecordSubmittedFrame({ .m_Revision = 7 }, 21));
			batch.m_Items.push_back(receipt->GetSnapshot());
			const Json wire = SerializeApplicationControlParameterBatch(batch);
			context.Check(wire["terminal"] && wire["succeeded"] && wire["values"][0]["value"] == 2.0f &&
				wire["items"][0]["requested"] == 20 && wire["items"][0]["applied"] == 20 &&
				wire["items"][0]["published"] == 20 && wire["items"][0]["firstConsumed"] == 21 &&
				wire["items"][0]["identity"]["revision"] == 7,
				"Serialization preserves values, exact identities and all four authoritative stamps");
			batch.m_Items[0].m_State = ParameterEffectState::Superseded;
			batch.m_Items[0].m_FirstConsumed.reset();
			const Json superseded = SerializeApplicationControlParameterBatch(batch);
			context.Check(!superseded["succeeded"] && superseded["items"][0]["firstConsumed"].is_null(),
				"A terminal superseded effect is not successful consumption");
			const Json status = Json::parse(SerializeApplicationControlStatus(709, {
				.m_ParametersAvailable = true, .m_ParameterRevision = 7, .m_ParameterBatch = &batch }));
			context.Check(status["capabilities"]["labParameterControl"]["maxBatches"] == 1 &&
				status["capabilities"]["labParameterControl"]["maxItems"] == 128 &&
				status["parameterControl"]["batch"] == superseded && status["parameterControl"]["revision"] == 7,
				"Status advertises bounded parameter support and borrows the actual receipt");
			context.Check(!status["capabilities"]["labParameterControl"].contains("scheduledEvents") &&
				status["capabilities"]["sequenceImmediateEvents"] == true &&
				status["capabilities"]["sequenceDeferredEvents"] == true,
				"Sequence capabilities alone advertise scheduled events; the Lab-only object does not contradict them");
			context.Check(status["capabilities"]["sessionParameterControl"]["sources"] == Json::array({ "host", "lab" }) &&
				status["capabilities"]["sessionParameterControl"]["mixedSources"] &&
				status["capabilities"]["sessionParameterControl"]["maxBatches"] == 1,
				"The additive session capability discovers host and mixed-source control without changing protocol v4");
			batch.m_Source = "mixed";
			batch.m_Impact = LabChangeImpact::RebuildScene;
			context.Check(SerializeApplicationControlParameterBatch(batch)["impact"] == "RebuildScene" &&
				status["capabilities"]["sessionParameterControl"]["effectClasses"].back() == "Rebuild" &&
				!status["capabilities"]["sessionParameterControl"]["rebuildWithDeferred"],
				"Rebuild discovery and batch responses preserve supported effect classes and actual change impact");
			context.Check(SerializeApplicationControlParameterBatch(batch)["source"] == "mixed" &&
				SerializeApplicationControlParameterBatch(batch)["contentId"] == "gglab.lab.fixture",
				"A mixed-source receipt retains its captured content identity");
		}

		void RunDeferredParameterSerializationTests(SelfTestContext& context) noexcept
		{
			ParameterBatchStatus batch{ .m_BatchId = 10, .m_Revision = 8,
				.m_ContentId = "gglab.lab.fixture", .m_State = ParameterBatchState::Applying };
			auto receipt = ParameterEffectReceipt::Begin(LabParameterId("lighting"), ParameterEffectClass::Deferred, 8, 30);
			const ParameterStateIdentity identity{ 8, "lighting", 11 };
			GGLAB_UNUSED(receipt->RecordApplied(ParameterEffectClass::Deferred, identity, 30));
			batch.m_Items.push_back(receipt->GetSnapshot());
			const Json pending = SerializeApplicationControlParameterBatch(batch);
			context.Check(!pending["terminal"] && pending["succeeded"].is_null() &&
				pending["items"][0]["declaredClass"] == "Deferred" && pending["items"][0]["state"] == "Applied" &&
				pending["items"][0]["identity"]["domain"] == "lighting" &&
				pending["items"][0]["identity"]["generation"] == 11 &&
				pending["items"][0]["published"].is_null() && pending["items"][0]["firstConsumed"].is_null(),
				"An applied Deferred wire receipt retains domain identity without claiming publication or consumption");
			GGLAB_UNUSED(receipt->RecordPublished(identity, 34));
			GGLAB_UNUSED(receipt->RecordSubmittedFrame(identity, 35));
			batch.m_State = ParameterBatchState::Finished;
			batch.m_Items[0] = receipt->GetSnapshot();
			const Json consumed = SerializeApplicationControlParameterBatch(batch);
			context.Check(consumed["succeeded"] && consumed["items"][0]["applied"] == 30 &&
				consumed["items"][0]["published"] == 34 && consumed["items"][0]["firstConsumed"] == 35,
				"Deferred serialization preserves distinct application, publication and actual consuming frames");
		}

		void RunParseTests(SelfTestContext& context) noexcept
		{
			const auto status = ParseApplicationControlRequest(R"({"protocol":4,"id":7,"command":"status"})");
			context.Check(status.m_Request && status.m_Request->m_Id == 7 &&
				status.m_Request->m_Command == ApplicationControlCommand::Status,
				"A v4 status request parses with its id");
			const auto capture = ParseApplicationControlRequest(
				R"({"protocol":4,"id":8,"command":"capture","source":"composited",)"
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
			const auto defaults = ParseApplicationControlRequest(R"({"protocol":4,"id":9,"command":"capture"})");
			context.Check(defaults.m_Request && defaults.m_Request->m_Capture.m_Timing == FrameCaptureTiming::AfterReady &&
				defaults.m_Request->m_Capture.m_SettleFrames == 8 &&
				std::get<ImageCaptureProduct>(defaults.m_Request->m_Capture.m_RequiredProducts[0]).m_Source == CaptureImageSource::Scene &&
				defaults.m_Request->m_Capture.m_WaitForCapacity,
				"Bare capture builds one settled scene product that waits for temporary execution capacity");
			const auto diagnostic = ParseApplicationControlRequest(
				R"({"protocol":4,"id":14,"command":"capture","source":"diagnostic","diagnosticTap":"temporal-history-weight"})");
			context.Check(diagnostic.m_Request &&
				std::get<DiagnosticCaptureProduct>(diagnostic.m_Request->m_Capture.m_RequiredProducts[0]).m_Tap == PostProcessDebugTap::TemporalHistoryWeight,
				"Diagnostic convenience capture directly builds its typed tap");
			const auto graph = ParseApplicationControlRequest(R"({"protocol":4,"id":50,"command":"graph","summary":true})");
			context.Check(graph.m_Request && graph.m_Request->m_Command == ApplicationControlCommand::Graph &&
				std::get<GraphCaptureProduct>(graph.m_Request->m_Capture.m_RequiredProducts[0]).m_Summary,
				"Graph builds a required graph product with a typed summary flag");
			const auto resource = ParseApplicationControlRequest(
				R"({"protocol":4,"id":60,"command":"export","targets":[{"resource":"Color","at":{"before":"Repeated"},"occurrence":2,"mip":1,"slices":[2,3]}],"graph":true})");
			context.Check(resource.m_Request && resource.m_Request->m_Capture.m_RequiredProducts.size() == 2 &&
				std::holds_alternative<GraphCaptureProduct>(resource.m_Request->m_Capture.m_RequiredProducts[1]) &&
				std::get<ResourceCaptureProduct>(resource.m_Request->m_Capture.m_RequiredProducts[0]).m_Targets[0].m_Occurrence == 2 &&
				std::get<ResourceCaptureProduct>(resource.m_Request->m_Capture.m_RequiredProducts[0]).m_Targets[0].m_SliceCount == 3,
				"Export builds two required products instead of reconstructing a legacy group");
			const auto multi = ParseApplicationControlRequest(
				R"({"protocol":4,"id":100,"command":"capture","products":[{"kind":"image","source":"scene"},{"kind":"diagnostic","diagnosticTap":"temporal-rejection"},{"kind":"graph"},{"kind":"resource","targets":[{"resource":"Color"}]}]})");
			context.Check(multi.m_Request && multi.m_Request->m_Capture.m_RequiredProducts.size() == 4 &&
				std::holds_alternative<ImageCaptureProduct>(multi.m_Request->m_Capture.m_RequiredProducts[0]) &&
				std::holds_alternative<DiagnosticCaptureProduct>(multi.m_Request->m_Capture.m_RequiredProducts[1]) &&
				std::holds_alternative<GraphCaptureProduct>(multi.m_Request->m_Capture.m_RequiredProducts[2]) &&
				std::holds_alternative<ResourceCaptureProduct>(multi.m_Request->m_Capture.m_RequiredProducts[3]),
				"Explicit required products retain plan order across all supported kinds");
			const auto query = ParseApplicationControlRequest(R"({"protocol":4,"id":10,"command":"result","jobId":4})");
			const auto consume = ParseApplicationControlRequest(R"({"protocol":4,"id":11,"command":"result","jobId":4,"consume":true})");
			const auto cancel = ParseApplicationControlRequest(R"({"protocol":4,"id":12,"command":"capture-cancel","jobId":4})");
			context.Check(query.m_Request && query.m_Request->m_JobId == 4 && !query.m_Request->m_Consume &&
				consume.m_Request && consume.m_Request->m_Consume && cancel.m_Request &&
				cancel.m_Request->m_Command == ApplicationControlCommand::CaptureCancel,
				"Query, explicit consumption and delivery cancellation share a job identity");

			const auto sequence = ParseApplicationControlRequest(
				R"({"protocol":4,"id":11,"command":"sequence","path":"SEQ_DollyDoorway","requiredContentId":"Demo.CoastalAtrium","captureFrames":[0,90,179],"source":"composited","outputDirectory":"D:/captures","label":"dolly","note":"baseline","exports":[{"frame":1,"targets":[{"resource":"Depth","aspect":"stencil"}],"graph":true}]})");
			context.Check(sequence.m_Request && sequence.m_Request->m_Sequence.m_CameraPathId == "SEQ_DollyDoorway" &&
				sequence.m_Request->m_Sequence.m_RequiredContentId == "Demo.CoastalAtrium" &&
				sequence.m_Request->m_Sequence.m_CaptureFrames == std::vector<uint32_t>{ 0, 90, 179 } &&
				sequence.m_Request->m_Sequence.m_CaptureSource == FrameCaptureSource::Composited &&
				sequence.m_Request->m_Sequence.m_OutputDirectory == std::filesystem::path("D:/captures") &&
				sequence.m_Request->m_Sequence.m_Label == "dolly" && sequence.m_Request->m_Sequence.m_Note == "baseline" &&
				sequence.m_Request->m_Sequence.m_Exports[0].m_Frame == 1 && sequence.m_Request->m_Sequence.m_Exports[0].m_IncludeGraph,
				"Sequence business fields and complete export groups remain available during migration");
			const auto reference = ParseApplicationControlRequest(
				R"({"protocol":4,"id":13,"command":"sequence","path":"SEQ_A","source":"diagnostic","diagnosticTap":"temporal-rejection","referenceSamples":64,"referenceTextureLodBias":-3})");
			context.Check(reference.m_Request && reference.m_Request->m_Sequence.m_CaptureSource == FrameCaptureSource::Diagnostic &&
				reference.m_Request->m_Sequence.m_DiagnosticTap == PostProcessDebugTap::TemporalRejection &&
				reference.m_Request->m_Sequence.m_ReferenceSamples == 64 && reference.m_Request->m_Sequence.m_ReferenceTextureLodBias == -3.0f,
				"Sequence diagnostic and supersampled reference controls remain intact");
			const auto evaluation = ParseApplicationControlRequest(
				R"({"protocol":4,"id":15,"command":"sequence","path":"SEQ_A","gpuTiming":true,"temporalAA":{"neighborhoodClampExpansion":1,"maxHistoryFeedback":0.9,"historyFilter":"bilinear","currentFilter":"point","motionSelection":"center","postTemporalView":"jittered","resolutionPreset":"quality","historyRectification":"variance-clip","varianceClipGamma":1.25,"historyAccumulation":"compatibility-age","historyRelaxation":2,"textureLodBiasOffset":-1.5,"enabled":false},"gtao":{"temporal":true,"temporalMaxSamples":24,"directionCount":1,"stepCount":8}})");
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
			context.Check(ParseApplicationControlRequest(R"({"protocol":4,"id":12,"command":"sequence-cancel"})").m_Request.has_value(),
				"Sequence cancellation remains a distinct business command");
			const auto events = ParseApplicationControlRequest(
				R"({"protocol":4,"id":80,"command":"sequence","path":"A","events":[{"frame":10,"set":{"view.exposure.ev100":2,"view.gtao.radius":1.5},"expectEffects":[]}]})");
			context.Check(events.m_Request && events.m_Request->m_Sequence.m_Events.size() == 1 &&
				events.m_Request->m_Sequence.m_Events[0].m_Frame == 10 &&
				events.m_Request->m_Sequence.m_Events[0].m_Values.size() == 2 &&
				events.m_Request->m_Sequence.m_Events[0].m_ExpectEffects.has_value(),
				"Sequence events retain exact frames, raw numeric assignments and explicit empty expectations");
			const auto deferredEvent = ParseApplicationControlRequest(
				R"({"protocol":4,"id":82,"command":"sequence","path":"A","events":[{"frame":10,"set":{"environment.profile":1},"expectEffects":["lighting"]}]})");
			context.Check(deferredEvent.m_Request &&
				deferredEvent.m_Request->m_Sequence.m_Events[0].m_ExpectEffects ==
					std::optional(std::vector<std::string>{ "lighting" }),
				"Deferred event expectations retain their owning domain without granting readiness permission");
			for (const auto* invalid : {
				R"({"frame":-1,"set":{"a":1}})",
				R"({"frame":0,"set":{}})",
				R"({"frame":0,"set":{"a":"bad"}})",
				R"({"frame":0,"set":{"a":1},"extra":true})",
				R"({"frame":0,"set":{"a":1},"expectEffects":["unknown"]})",
				R"({"frame":0,"set":{"a":1},"expectEffects":["lighting","lighting"]})",
				R"({"frame":0,"set":{"a":1},"expectEffects":[true]})",
				R"({"frame":0,"set":{"a":4294967296}})" })
			{
				context.Check(!ParseApplicationControlRequest(std::format(
					R"({{"protocol":4,"id":81,"command":"sequence","path":"A","events":[{}]}})", invalid)).m_Request,
					"Malformed or unsupported event input rejects before admission");
			}
			const auto watch = ParseApplicationControlRequest(
				R"({"protocol":4,"id":70,"command":"watch","mode":"report","claims":["boundary"],"targets":[{"resource":"Motion","at":{"before":"TAA"},"occurrence":2,"slices":[2,1]}]})");
			const auto buffer = ParseApplicationControlRequest(
				R"({"protocol":4,"id":71,"command":"watch","targets":[{"resource":"Values","buffer":{"stride":16,"validElementCount":3,"fields":[{"offset":0,"type":"uint32"},{"offset":4,"type":"float32","count":2},{"offset":12,"type":"float16","count":2}]}}]})");
			const auto off = ParseApplicationControlRequest(R"({"protocol":4,"id":72,"command":"watch","enabled":false})");
			const auto production = ParseApplicationControlRequest(R"({"protocol":4,"id":73,"command":"watch","claims":["production"]})");
			context.Check(watch.m_Request && !watch.m_Request->m_Watch.m_Production &&
				watch.m_Request->m_Watch.m_Targets[0].m_Address.m_Occurrence == 2u && buffer.m_Request &&
				buffer.m_Request->m_Watch.m_Targets[0].m_BufferLayout.m_Components.size() == 4 &&
				buffer.m_Request->m_Watch.m_Targets[0].m_BufferLayout.m_ScalarCount == 12 && off.m_Request &&
				!off.m_Request->m_Watch.m_Enabled && production.m_Request && !production.m_Request->m_Watch.m_Boundary && production.m_Request->m_Watch.m_Production,
				"Watch preserves typed addresses, valid floating buffer fields, disabled and production-only modes");
			const auto injection = ParseApplicationControlRequest(
				R"({"protocol":4,"id":74,"command":"watch","mode":"fail","inject":{"point":"pass-temporal-aa.csmain.history","value":"NegativeInfinity","x":12,"y":34,"component":3}})");
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
				R"({"protocol":3,"id":116,"command":"status"})",
				R"({"protocol":5,"id":3,"command":"status"})",
				R"({"protocol":4,"id":75,"command":"watch","inject":{"point":"unknown.point","value":"NaN","x":0,"y":0}})",
				R"({"protocol":4,"id":76,"command":"watch","inject":{"point":"pass-temporal-aa.csmain.history","value":"nan","x":0,"y":0}})",
				R"({"protocol":4,"id":77,"command":"watch","inject":{"point":"pass-temporal-aa.csmain.history","value":"NaN","x":0,"y":0,"component":4}})",
				R"({"protocol":4,"id":78,"command":"watch","inject":{"point":"pass-temporal-aa.csmain.history","value":"NaN","x":0}})",
				R"({"protocol":4,"id":80,"command":"watch","claims":[]})",
				R"({"protocol":4,"id":81,"command":"watch","claims":["boundary","boundary"]})",
				R"({"protocol":4,"id":82,"command":"watch","claims":["other"]})",
				R"({"protocol":4,"id":83,"command":"watch","mode":"fatal"})",
				R"({"protocol":4,"id":84,"command":"watch","enabled":1})",
				R"({"protocol":4,"id":85,"command":"watch","enabled":false,"targets":[]})",
				R"({"protocol":4,"id":86,"command":"watch","target":[]})",
				R"({"protocol":4,"id":87,"command":"watch","targets":[null]})",
				R"({"protocol":4,"id":88,"command":"watch","targets":[{"resource":"A","buffer":{}}]})",
				R"({"protocol":4,"id":89,"command":"watch","targets":[{"resource":"A","buffer":{"stride":4,"validElementCount":2,"fields":[{"offset":0,"type":"uint32"}]}}]})",
				R"({"protocol":4,"id":90,"command":"watch","targets":[{"resource":"A","buffer":{"stride":4,"validElementCount":-1,"fields":[{"offset":0,"type":"float32"}]}}]})",
				R"({"protocol":4,"id":91,"command":"watch","targets":[{"resource":"A","buffer":{"stride":4,"validElementCount":1,"fields":[{"offset":0,"type":"float32","count":4294967295}]}}]})",
				R"({"protocol":4,"id":92,"command":"watch","targets":[{"resource":"A","buffer":{"stride":8,"validElementCount":1,"fields":[{"offset":0,"type":"float32","count":2},{"offset":4,"type":"float32"}]}}]})",
				R"({"protocol":4,"id":93,"command":"watch","targets":[{"resource":"A","buffer":{"stride":4,"validElementCount":1,"fields":[{"offset":0,"type":"float32","extra":1}]}}]})",
				R"({"protocol":4,"id":62,"command":"export","targets":[]})",
				R"({"protocol":4,"id":63,"command":"export","targets":[{"resource":"A","at":{"before":"P","after":"P"}}]})",
				R"({"protocol":4,"id":64,"command":"export","targets":[{"resource":"A","occurrence":0}]})",
				R"({"protocol":4,"id":65,"command":"export","targets":[{"resource":"A","slices":[0,0]}]})",
				R"({"protocol":4,"id":66,"command":"export","targets":[{"resource":"A","mipp":1}]})",
				R"({"protocol":4,"id":67,"command":"export","targets":[{"resource":"A","aspect":"all"}]})",
				R"({"protocol":4,"id":68,"command":"export","targets":[{"resource":"A"}],"graph":1})",
				R"({"protocol":4,"command":"status"})",
				R"({"protocol":4,"id":4,"command":"reboot"})",
				R"({"protocol":4,"id":5,"command":"status","extra":1})",
				R"({"protocol":4,"id":6,"command":"capture","source":"hud"})",
				R"({"protocol":4,"id":7,"command":"capture","settleFrames":-1})",
				R"({"protocol":4,"id":8,"command":"capture","outputDirectory":"relative"})",
				R"({"protocol":4,"id":9,"command":"capture","lable":"typo"})",
				R"({"protocol":4,"id":10,"command":"result"})",
				R"({"protocol":4,"id":11,"command":"result","jobId":0})",
				R"({"protocol":4,"id":12,"command":"sequence"})",
				R"({"protocol":4,"id":13,"command":"sequence","path":"A","captureFrames":[-1]})",
				R"({"protocol":4,"id":14,"command":"sequence","path":"A","captureFrames":3})",
				R"({"protocol":4,"id":15,"command":"sequence","path":"A","frames":[1]})",
				R"({"protocol":4,"id":16,"command":"sequence-cancel","path":"A"})",
				R"({"protocol":4,"id":17,"command":"capture","source":"diagnostic"})",
				R"({"protocol":4,"id":18,"command":"capture","diagnosticTap":"temporal-rejection"})",
				R"({"protocol":4,"id":19,"command":"sequence","path":"A","source":"diagnostic","diagnosticTap":"bloom-result"})",
				R"({"protocol":4,"id":20,"command":"sequence","path":"A","referenceSamples":5000})",
				R"({"protocol":4,"id":21,"command":"sequence","path":"A","temporalAA":{"maxHistoryFeedback":1.5}})",
				R"({"protocol":4,"id":22,"command":"sequence","path":"A","temporalAA":{"historyFilter":1}})",
				R"({"protocol":4,"id":23,"command":"sequence","path":"A","temporalAA":{"velocityWeightScale":"x"}})",
				R"({"protocol":4,"id":24,"command":"sequence","path":"A","temporalAA":[]})",
				R"({"protocol":4,"id":25,"command":"sequence","path":"A","gpuTiming":1})",
				R"({"protocol":4,"id":26,"command":"sequence","path":"A","temporalAA":{"historyFilter":"bicubic"}})",
				R"({"protocol":4,"id":27,"command":"sequence","path":"A","temporalAA":{"historyFilter":"catmull-rom"}})",
				R"({"protocol":4,"id":28,"command":"sequence","path":"A","temporalAA":{"currentFilter":"box"}})",
				R"({"protocol":4,"id":29,"command":"sequence","path":"A","temporalAA":{"currentFilter":"gaussian-narrow"}})",
				R"({"protocol":4,"id":30,"command":"sequence","path":"A","temporalAA":{"motionSelection":"dilated"}})",
				R"({"protocol":4,"id":31,"command":"sequence","path":"A","temporalAA":{"motionSelection":"closest-depth-correspondence"}})",
				R"({"protocol":4,"id":32,"command":"sequence","path":"A","temporalAA":{"textureLodBiasOffset":-3}})",
				R"({"protocol":4,"id":33,"command":"sequence","path":"A","referenceTextureLodBias":"low"})",
				R"({"protocol":4,"id":34,"command":"sequence","path":"A","temporalAA":{"enabled":0}})",
				R"({"protocol":4,"id":35,"command":"sequence","path":"A","temporalAA":{"postTemporalView":"none"}})",
				R"({"protocol":4,"id":36,"command":"sequence","path":"A","temporalAA":{"resolutionPreset":"balanced"}})",
				R"({"protocol":4,"id":37,"command":"sequence","path":"A","temporalAA":{"historyRectification":"variance"}})",
				R"({"protocol":4,"id":38,"command":"sequence","path":"A","temporalAA":{"varianceClipGamma":8}})",
				R"({"protocol":4,"id":39,"command":"sequence","path":"A","temporalAA":{"historyAccumulation":"age"}})",
				R"({"protocol":4,"id":40,"command":"sequence","path":"A","temporalAA":{"historyRelaxation":5}})",
				R"({"protocol":4,"id":41,"command":"sequence","path":"A","gtao":{"temporal":1}})",
				R"({"protocol":4,"id":42,"command":"sequence","path":"A","gtao":{"temporalMaxSamples":0}})",
				R"({"protocol":4,"id":43,"command":"sequence","path":"A","gtao":{"temporalMaxSamples":2.5}})",
				R"({"protocol":4,"id":44,"command":"sequence","path":"A","gtao":{"radius":2}})",
				R"({"protocol":4,"id":45,"command":"sequence","path":"A","gtao":{"directionCount":9}})",
				R"({"protocol":4,"id":51,"command":"graph","summary":1})",
				R"({"protocol":4,"id":52,"command":"graph","source":"scene"})",
				R"({"protocol":4,"id":53,"command":"graph","extra":true})",
				R"({"protocol":4,"id":54,"command":"capture","summary":true})",
				R"({"protocol":4,"id":101,"command":"capture","wait":false})",
				R"({"protocol":4,"id":102,"command":"result","requestId":1})",
				R"({"protocol":4,"id":103,"command":"result","jobId":1,"consume":1})",
				R"({"protocol":4,"id":104,"command":"capture-cancel","jobId":1,"consume":true})",
				R"({"protocol":4,"id":105,"command":"result","jobId":1,"extra":1})",
				R"({"protocol":4,"id":106,"command":"capture","products":[]})",
				R"({"protocol":4,"id":107,"command":"capture","products":[{"kind":"image","source":"diagnostic"}]})",
				R"({"protocol":4,"id":108,"command":"capture","products":[{"kind":"image","source":"scene","optional":true}]})",
				R"({"protocol":4,"id":109,"command":"capture","products":[{"kind":"diagnostic"}]})",
				R"({"protocol":4,"id":110,"command":"capture","products":[{"kind":"graph","summary":1}]})",
				R"({"protocol":4,"id":111,"command":"capture","products":[{"kind":"resource","targets":[]}]})",
				R"({"protocol":4,"id":112,"command":"capture","products":[{"kind":"other"}]})",
				R"({"protocol":4,"id":113,"command":"capture","products":[{"kind":"graph"}],"source":"scene"})",
				R"({"protocol":4,"id":114,"command":"graph","products":[{"kind":"graph"}]})",
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
			Json oversized = { { "protocol", 4 }, { "id", 115 }, { "command", "capture" }, { "products", Json::array() } };
			for (uint32_t index = 0; index < 9; ++index)
			{
				oversized["products"].push_back({ { "kind", "image" }, { "source", "scene" } });
			}
			context.Check(!ParseApplicationControlRequest(oversized.dump()).m_Request, "Product array is bounded before admission");
		}

		void RunSerializeTests(SelfTestContext& context) noexcept
		{
			const auto error = Json::parse(SerializeApplicationControlError(3, "bad \"input\""));
			context.Check(error["protocol"] == 4 && error["id"] == 3 && error["ok"] == false && error["error"] == "bad \"input\"",
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
				.m_FrameSerial = 73, .m_FrameIndex = 42, .m_ReferenceViewIds = { "CAM_A", "CAM_B" } };
			frame.m_Readiness.Add("lab", FrameCaptureGateState::Pending, "Warming up.");
			frame.m_HostViewControl = { .m_RunIdentity = "0123456789abcdef0123456789abcdef",
				.m_ContentIdentity = 481, .m_HostViewRevision = 12 };
			frame.m_HostViewProjection = { { .m_Id = LabParameterId("view.taa.enabled"), .m_Type = LabParameterType::Bool,
				.m_Value = false, .m_Source = HostViewValueSource::Host, .m_InheritedSource = HostViewValueSource::Tooling,
				.m_Override = LabValue(false) } };
			frame.m_Temporal.m_Status = "disabled";
			frame.m_Temporal.m_DisableReason = "not-requested";
			frame.m_Temporal.m_Consumers = { { .m_Name = "temporal-aa", .m_Status = "disabled",
				.m_DisableReason = "not-requested" } };
			frame.m_Outcome = FrameCaptureFrameOutcome::Submitted;
			const auto status = Json::parse(SerializeApplicationControlStatus(4, {
				.m_SessionId = "s1", .m_ProcessId = 99, .m_Hidden = true, .m_Width = 640, .m_Height = 360,
				.m_CaptureIntake = { .m_ActiveJobs = 2, .m_ReservedReceipts = 3 }, .m_SettledFrames = 5, .m_Frame = &frame,
				.m_HostViewControl = { .m_ContentIdentity = 481, .m_HostViewRevision = 13 } }));
			const auto& hostView = status["frame"]["hostView"];
			context.Check(status["frame"]["outcome"] == "submitted" &&
				status["frame"]["controlState"]["runIdentity"] == "0123456789abcdef0123456789abcdef" &&
				status["frame"]["controlState"]["hostViewRevision"] == 12 &&
				status["parameterControl"]["controlState"]["runIdentity"].is_null() &&
				status["parameterControl"]["controlState"]["hostViewRevision"] == 13 &&
				hostView.size() == 1 && hostView[0]["id"] == "view.taa.enabled" && hostView[0]["value"] == false &&
				hostView[0]["source"] == "host" && hostView[0]["inheritedSource"] == "tooling" &&
				hostView[0]["hostOverride"] == Json({ { "present", true }, { "value", false } }) &&
				status["frame"]["temporal"]["consumers"][0]["name"] == "temporal-aa" &&
				status["frame"]["temporal"]["status"] == "disabled",
				"Status separates the planned frame's control stamp, attribution and outcome from live control state");
			context.Check(status["capabilities"]["hostViewControlState"]["version"] == 1 &&
				status["capabilities"]["hostViewControlState"]["valueSources"].size() == 6 &&
				status["capabilities"]["hostViewControlState"]["frameOutcomes"].size() == 3,
				"Host view control observation is advertised as an additive capability");
			const HostViewProjectionEntry inherited{ .m_Id = LabParameterId("view.gtao.radius"),
				.m_Type = LabParameterType::Float, .m_Value = 1.5f };
			context.Check(SerializeApplicationControlHostViewProjection({ &inherited, 1 })[0]["hostOverride"] ==
				Json({ { "present", false } }),
				"An absent Host override carries no value");
			context.Check(status["session"]["pid"] == 99 && status["frame"]["labId"] == "gglab.lab.culling" &&
				status["frame"]["ready"] == false && status["frame"]["settledFrames"] == 5 &&
				status["frame"]["gates"][0]["state"] == "pending" && status["frame"]["fixedDeltaTime"].is_null() &&
				status["frame"]["referenceViews"] == Json::array({ "CAM_A", "CAM_B" }), "Status preserves session and frame readiness");
			context.Check(status["frame"]["frameSerial"] == 73 && status["frame"]["frameIndex"] == 42,
				"Status preserves the render serial separately from the simulation frame index");
			context.Check(!status["capture"].contains("legacyUnfinishedOutputs") &&
				!status["capture"].contains("legacyRetainedResults"),
				"Status exposes only current job ownership without retired output counters");
			context.Check(status["capabilities"]["captureJobs"] == true && status["capabilities"]["sequenceCaptureJobs"] == true &&
				status["capabilities"]["resultConsumption"] == "explicit" && status["capture"]["activeJobs"] == 2 &&
				status["capture"]["reservedReceipts"] == 3 && status["capture"]["limits"]["receipts"] == 128 &&
				status["capture"]["limits"]["activeJobs"] == 64 && status["capture"]["limits"]["productsPerJob"] == 8 &&
				status["capture"]["limits"]["resourceTargetsPerJob"] == 64, "Capabilities advertise actual count policy for the final bounded job contract");
			LabSnapshot lab;
			lab.m_ActiveLabId.m_Name = "gglab.lab.gi_reference";
			lab.m_FrameInSession = 17;
			lab.m_Diagnostics.m_Metrics.push_back({ "Probe progress", "completed=1" });
			lab.m_Diagnostics.m_Checks.push_back({ "Finite", LabDiagnosticCheckStatus::Passed, "All samples" });
			const auto labStatus = Json::parse(SerializeApplicationControlStatus(5, { .m_Lab = lab }));
			context.Check(labStatus["lab"]["id"] == "gglab.lab.gi_reference" &&
				labStatus["lab"]["frameInSession"] == 17 && labStatus["lab"]["metrics"][0]["value"] == "completed=1" &&
				labStatus["lab"]["checks"][0]["status"] == "passed",
				"Status preserves immutable Lab diagnostics without interpreting metric strings");
			const auto noFrame = Json::parse(SerializeApplicationControlStatus(5, {}));
			context.Check(noFrame["lab"].is_null(), "Unavailable Lab diagnostics remain absent");
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
				.m_Scopes = {
					{ .m_Name = "PostProcess.TemporalAA", .m_Milliseconds = { 0.25 },
						.m_Kind = GpuProfileScopeKind::RenderGraphPass, .m_Occurrence = 2 },
					{ .m_Name = "PostProcess.TemporalAA", .m_Milliseconds = { 0.5 } },
				} };
			const auto timing = Json::parse(SerializeApplicationControlSequence(12, sequence))["sequence"]["gpuTiming"];
			context.Check(timing["frames"] == 2 && timing["frame"]["medianMs"] == 2.0 &&
				timing["scopes"][0]["name"] == "PostProcess.TemporalAA" && timing["scopes"][0]["p90Ms"] == 0.25,
				"Sequence GPU timing retains per-scope summaries");
			context.Check(timing["scopes"][0]["kind"] == "pass" &&
				timing["scopes"][0]["occurrence"] == 2 && timing["scopes"][1]["kind"] == "other" &&
				timing["scopes"][1]["occurrence"].is_null(),
				"Sequence GPU timing exposes pass instance identities separately from other scopes");
			sequence.m_State = FrameSequenceState::Failed;
			sequence.m_TimingPublication = FrameSequenceArtifactPublication{
				.m_Outcome = CapturePublicationOutcome::Indeterminate, .m_Failure = "Publication is uncertain." };
			const auto tooLate = Json::parse(SerializeApplicationControlSequenceCancellation(
				13, &sequence, FrameSequenceCancelVerdict::TooLate));
			context.Check(tooLate["cancellation"] == "TooLate" && tooLate["sequence"]["state"] == "failed" &&
				tooLate["sequence"]["timingPublication"]["outcome"] == "Indeterminate" &&
				tooLate["sequence"]["timingPublication"]["pending"] == false,
				"TooLate responses retain the actual timing publication failure and outcome");
			const auto noSequence = Json::parse(SerializeApplicationControlSequenceCancellation(
				14, nullptr, FrameSequenceCancelVerdict::NotFound));
			context.Check(noSequence["ok"] == true && noSequence["cancellation"] == "NotFound" &&
				noSequence["sequence"].is_null(), "Sequence cancellation reports NotFound without inventing a sequence");
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

		void RunRunIdentityCheckTests(SelfTestContext& context) noexcept
		{
			constexpr std::string_view run = "0123456789abcdef0123456789abcdef";
			const auto set = ParseApplicationControlRequest(
				R"({"protocol":4,"id":30,"command":"set","expectedRunIdentity":"0123456789abcdef0123456789abcdef",)"
				R"("changes":[{"id":"view.taa.enabled","type":"bool","value":false}]})");
			const auto result = ParseApplicationControlRequest(
				R"({"protocol":4,"id":31,"command":"result","jobId":4,"consume":true,)"
				R"("expectedRunIdentity":"0123456789abcdef0123456789abcdef"})");
			const auto stop = ParseApplicationControlRequest(
				R"({"protocol":4,"id":32,"command":"stop","expectedRunIdentity":"0123456789abcdef0123456789abcdef"})");
			context.Check(set.m_Request && set.m_Request->m_ExpectedRunIdentity == run &&
				set.m_Request->m_ParameterChanges.size() == 1 &&
				result.m_Request && result.m_Request->m_JobId == 4 && result.m_Request->m_Consume &&
				result.m_Request->m_ExpectedRunIdentity == run &&
				stop.m_Request && stop.m_Request->m_ExpectedRunIdentity == run,
				"Protected commands accept an expected run beside their exact fields");
			const auto legacy = ParseApplicationControlRequest(R"({"protocol":4,"id":33,"command":"stop"})");
			context.Check(legacy.m_Request && !legacy.m_Request->m_ExpectedRunIdentity,
				"Requests without an expected run keep their existing meaning");
			for (const auto* invalid : {
				R"({"protocol":4,"id":34,"command":"status","expectedRunIdentity":"0123456789abcdef0123456789abcdef"})",
				R"({"protocol":4,"id":35,"command":"parameters","expectedRunIdentity":"0123456789abcdef0123456789abcdef"})",
				R"({"protocol":4,"id":36,"command":"stop","expectedRunIdentity":"0123456789ABCDEF0123456789ABCDEF"})",
				R"({"protocol":4,"id":37,"command":"stop","expectedRunIdentity":"0123456789abcdef0123456789abcde"})",
				R"({"protocol":4,"id":38,"command":"stop","expectedRunIdentity":7})",
				R"({"protocol":4,"id":39,"command":"stop","expectedRunIdentity":"0123456789abcdef0123456789abcdef","x":1})" })
			{
				context.Check(!ParseApplicationControlRequest(invalid).m_Request,
					"Queries, malformed run identities and unknown fields reject the request");
			}

			context.Check(!CheckApplicationControlRunIdentity(*legacy.m_Request, "") &&
				!CheckApplicationControlRunIdentity(*stop.m_Request, run),
				"Unchecked requests and the matching run proceed");
			const auto changed = Json::parse(*CheckApplicationControlRunIdentity(*stop.m_Request,
				"ffffffffffffffffffffffffffffffff"));
			context.Check(!changed["ok"] && changed["id"] == 32 && changed["errorCode"] == "ExpectedRunChanged" &&
				changed["runIdentity"] == "ffffffffffffffffffffffffffffffff",
				"A different run is rejected with a machine-readable code and the current run");
			const auto unavailable = Json::parse(*CheckApplicationControlRunIdentity(*set.m_Request, ""));
			context.Check(!unavailable["ok"] && unavailable["errorCode"] == "RunIdentityUnavailable",
				"A session without a run identity rejects run-checked requests");

			const auto status = Json::parse(SerializeApplicationControlStatus(40, {}));
			const auto& commands = status["capabilities"]["runIdentityChecks"]["commands"];
			context.Check(status["capabilities"]["runIdentityChecks"]["version"] == 1 && commands.size() == 11 &&
				!AcceptsApplicationControlExpectedRun(ApplicationControlCommand::Status) &&
				!AcceptsApplicationControlExpectedRun(ApplicationControlCommand::Parameters) &&
				AcceptsApplicationControlExpectedRun(ApplicationControlCommand::Sequence),
				"Run checks are advertised for every protected command");
		}

		void RunHostViewOverrideTests(SelfTestContext& context) noexcept
		{
			constexpr std::string_view run = "0123456789abcdef0123456789abcdef";
			const auto request = [](std::string_view expected, std::string_view overrides)
				{
					return ParseApplicationControlRequest(std::format(
						R"({{"protocol":4,"id":50,"command":"host-view-overrides","expected":{},"overrides":{}}})",
						expected, overrides));
				};
			constexpr std::string_view stamp =
				R"({"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":481,"hostViewRevision":12})";
			const auto parsed = request(stamp,
				R"([{"id":"view.taa.enabled","type":"bool","present":true,"value":false},)"
				R"({"id":"view.gtao.radius","type":"float","present":false}])");
			const auto* overrides = parsed.m_Request ? &parsed.m_Request->m_HostViewOverrides : nullptr;
			context.Check(parsed.m_Request && parsed.m_Request->m_Command == ApplicationControlCommand::HostViewOverrides &&
				parsed.m_Request->m_ExpectedHostView.m_RunIdentity == run &&
				parsed.m_Request->m_ExpectedHostView.m_ContentIdentity == 481 &&
				parsed.m_Request->m_ExpectedHostView.m_HostViewRevision == 12 &&
				parsed.m_Request->m_ExpectedRunIdentity == run && overrides->size() == 2 &&
				(*overrides)[0].m_Type == LabParameterType::Bool && (*overrides)[0].m_Value &&
				!std::get<bool>(*(*overrides)[0].m_Value) &&
				(*overrides)[1].m_Type == LabParameterType::Float && !(*overrides)[1].m_Value,
				"Host view operations parse a complete stamp and typed present/absent states");
			const auto changed = CheckApplicationControlRunIdentity(*parsed.m_Request, "ffffffffffffffffffffffffffffffff");
			context.Check(changed && Json::parse(*changed)["errorCode"] == "ExpectedRunChanged" &&
				!CheckApplicationControlRunIdentity(*parsed.m_Request, run),
				"The expected stamp's run feeds the check that precedes admission");

			constexpr std::string_view valid = R"([{"id":"view.gtao.radius","type":"float","present":false}])";
			for (const auto& [expected, states] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
				{ R"({"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":481})", valid },
				{ R"({"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":481,"hostViewRevision":12,"x":1})", valid },
				{ R"({"runIdentity":"0123456789ABCDEF0123456789ABCDEF","contentIdentity":481,"hostViewRevision":12})", valid },
				{ R"({"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":-1,"hostViewRevision":12})", valid },
				{ stamp, R"([])" },
				{ stamp, R"([{"id":"view.gtao.radius","type":"float","present":false,"value":1.0}])" },
				{ stamp, R"([{"id":"view.gtao.radius","type":"float","present":true}])" },
				{ stamp, R"([{"id":"view.gtao.radius","type":"float","present":1}])" },
				{ stamp, R"([{"id":"view.gtao.radius","type":"float","value":1.0}])" },
				{ stamp, R"([{"id":"view.gtao.radius","type":"real","present":false}])" },
				{ stamp, R"([{"id":"view.gtao.radius","type":"float","present":true,"value":"1"}])" },
				{ stamp, R"([{"id":"view.gtao.radius","type":"float","present":false,"x":1}])" },
				{ stamp, R"([{"id":"a","type":"bool","present":false},{"id":"a","type":"bool","present":false}])" } })
			{
				context.Check(!request(expected, states).m_Request,
					"Partial stamps and malformed or ambiguous override states reject the request");
			}
			context.Check(!ParseApplicationControlRequest(
				R"({"protocol":4,"id":51,"command":"host-view-overrides","expected":{"runIdentity":"0123456789abcdef0123456789abcdef",)"
				R"("contentIdentity":481,"hostViewRevision":12},"overrides":[{"id":"a","type":"bool","present":false}],)"
				R"("expectedRunIdentity":"0123456789abcdef0123456789abcdef"})").m_Request &&
				!ParseApplicationControlRequest(
					R"({"protocol":4,"id":52,"command":"host-view-overrides","overrides":[{"id":"a","type":"bool","present":false}]})").m_Request,
				"The operation names its run only in the required stamp");

			ParameterBatchStatus batch;
			batch.m_BatchId = 7;
			batch.m_Source = "host";
			batch.m_HostViewOperation = HostViewOverrideOperation{
				.m_Expected = { std::string(run), 481, 12 },
				.m_Overrides = { { LabParameterId("view.taa.enabled"), LabParameterType::Bool, false } },
			};
			const Json queued = SerializeApplicationControlParameterBatch(batch);
			context.Check(queued["overrides"].size() == 1 && queued["overrides"][0]["present"] == true &&
				queued["overrides"][0]["value"] == false && queued["overrides"][0]["type"] == "bool" &&
				queued["controlCommit"]["expected"]["hostViewRevision"] == 12 && queued["controlCommit"]["before"].is_null() &&
				queued["controlCommit"]["after"].is_null() && queued["controlCommit"]["changed"].is_null() &&
				queued["values"].empty(),
				"A queued operation reports its requested states without fabricated commit evidence");
			batch.m_State = ParameterBatchState::Finished;
			batch.m_HostViewOperation->m_Before = HostViewControlStamp{ std::string(run), 481, 12 };
			batch.m_HostViewOperation->m_After = HostViewControlStamp{ std::string(run), 481, 13 };
			batch.m_HostViewOperation->m_Changes = { { LabParameterId("view.taa.enabled"), std::nullopt, false } };
			const Json committed = SerializeApplicationControlParameterBatch(batch);
			context.Check(committed["controlCommit"]["before"]["hostViewRevision"] == 12 &&
				committed["controlCommit"]["after"]["hostViewRevision"] == 13 && committed["controlCommit"]["changed"] == true &&
				committed["controlCommit"]["changes"][0]["before"]["present"] == false &&
				committed["controlCommit"]["changes"][0]["after"]["value"] == false &&
				committed["controlCommit"]["conflict"].is_null(),
				"A committed operation reports before/after stamps and the changed override states");
			batch.m_HostViewOperation->m_After.reset();
			batch.m_HostViewOperation->m_Changes.clear();
			batch.m_HostViewOperation->m_Conflict = "ExpectedHostViewRevisionMismatch";
			batch.m_State = ParameterBatchState::Failed;
			const Json conflict = SerializeApplicationControlParameterBatch(batch);
			context.Check(conflict["controlCommit"]["after"].is_null() && conflict["controlCommit"]["changed"] == false &&
				conflict["controlCommit"]["conflict"] == "ExpectedHostViewRevisionMismatch" &&
				!SerializeApplicationControlParameterBatch(ParameterBatchStatus{}).contains("controlCommit"),
				"A conflict applies nothing, and legacy batches carry no control-commit record");

			const auto status = Json::parse(SerializeApplicationControlStatus(53, {}));
			const auto& capability = status["capabilities"]["hostViewOverrides"];
			const auto& checked = status["capabilities"]["runIdentityChecks"]["commands"];
			context.Check(capability["version"] == 1 && capability["command"] == "host-view-overrides" &&
				capability["groups"].size() == 3 && capability["maxItems"] == 128 && capability["expected"].size() == 3 &&
				capability["errorCodes"].size() == ApplicationControlHostViewOverrideErrorCodes.size() &&
				std::ranges::find(checked, Json("host-view-overrides")) == checked.end() &&
				!AcceptsApplicationControlExpectedRun(ApplicationControlCommand::HostViewOverrides),
				"The guarded operation is advertised with its stamp, limits and error codes");
			const auto rejected = Json::parse(SubmitApplicationControlHostViewOverrides(54, {}, {}, nullptr));
			context.Check(!rejected["ok"] && rejected["errorCode"] == "NoParameterSource" &&
				rejected["controlState"].is_null(),
				"Rejections carry a machine-readable code beside the error text");
		}

		void RunProtectedAssignmentTests(SelfTestContext& context) noexcept
		{
			const ParameterControlStamp stamp{
				.m_RunIdentity = "0123456789abcdef0123456789abcdef", .m_ContentIdentity = 21, .m_ActivationRevision = 3,
				.m_Domain = ParameterControlDomain::Lab, .m_LabRevision = 8, .m_ValuesRevision = 9,
				.m_WorldRevision = 13, .m_EnvironmentRevision = 17 };
			const Json document{
				{ "protocol", 4 }, { "id", 57 }, { "command", "assign" },
				{ "expected", SerializeApplicationControlDomainStamp(stamp) },
				{ "changes", Json::array({ { { "id", "radius" }, { "type", "float" }, { "value", 2.0f } } }) } };
			const auto parsed = ParseApplicationControlRequest(document.dump());
			context.Check(parsed.m_Request && parsed.m_Request->m_Command == ApplicationControlCommand::Assign &&
				parsed.m_Request->m_ExpectedParameterControl == stamp &&
				parsed.m_Request->m_ExpectedRunIdentity == stamp.m_RunIdentity &&
				parsed.m_Request->m_ParameterChanges.size() == 1,
				"Protected Assign parses a complete typed domain guard and binds subsequent polling to its run");
			for (const auto& field : { "runIdentity", "contentIdentity", "activationRevision", "domain", "labRevision",
				"valuesRevision", "worldRevision", "environmentRevision" })
			{
				auto partial = document;
				partial["expected"].erase(field);
				context.Check(!ParseApplicationControlRequest(partial.dump()).m_Request,
					"Partial domain control stamps reject before admission");
			}
			auto invalid = document;
			invalid["expected"]["domain"] = "host";
			context.Check(!ParseApplicationControlRequest(invalid.dump()).m_Request,
				"Protected Assign cannot reinterpret Host view ownership");
			invalid = document;
			invalid["expected"]["worldRevision"] = -1;
			context.Check(!ParseApplicationControlRequest(invalid.dump()).m_Request,
				"Signed or fractional control revisions are not accepted as unsigned identities");
			invalid = document;
			invalid["expectedRunIdentity"] = stamp.m_RunIdentity;
			context.Check(!ParseApplicationControlRequest(invalid.dump()).m_Request,
				"The protected stamp is the single run authority for Assign");
			if (parsed.m_Request)
			{
				const auto rejection = CheckApplicationControlRunIdentity(*parsed.m_Request,
					"ffffffffffffffffffffffffffffffff");
				context.Check(rejection && Json::parse(*rejection)["errorCode"] == "ExpectedRunChanged",
					"A restarted session rejects protected Assign without touching process-local state");
			}
			ParameterBatchStatus batch;
			batch.m_DomainOperation = ParameterControlOperation{ .m_Expected = stamp, .m_Before = stamp,
				.m_Committed = false, .m_Conflict = "ExpectedParameterControlMismatch" };
			const auto failed = SerializeApplicationControlParameterBatch(batch);
			context.Check(failed["domainControl"]["committed"] == false &&
				failed["domainControl"]["after"].is_null() &&
				failed["domainControl"]["conflict"] == "ExpectedParameterControlMismatch" &&
				!SerializeApplicationControlParameterBatch(ParameterBatchStatus{}).contains("domainControl"),
				"Domain commit observations are separate from receipts and absent on legacy results");
			const auto status = Json::parse(SerializeApplicationControlStatus(58, {}));
			context.Check(status["capabilities"]["protectedAssignments"]["domains"].size() == 2 &&
				status["capabilities"]["protectedAssignments"]["mixedDomains"] == false,
				"Capabilities advertise the bounded protected domain contract");
			const auto absent = Json::parse(SubmitApplicationControlParameters(59, {}, nullptr, &stamp));
			context.Check(!absent["ok"] && absent["errorCode"] == "NoParameterSource" &&
				absent["domainControlState"].is_null(),
				"Protected rejections report an error code and current domain availability");
		}

		void RunRequiredFrameStateTests(SelfTestContext& context) noexcept
		{
			const auto capture = [](std::string_view command, std::string_view required)
				{
					return ParseApplicationControlRequest(std::format(
						R"({{"protocol":4,"id":60,"command":"{}","requiredFrameState":{}}})", command, required));
				};
			constexpr std::string_view head =
				R"({"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":481,"minHostViewRevision":13,)";
			const auto parsed = capture("capture", std::format("{}{}", head,
				R"("values":[{"id":"view.taa.enabled","type":"bool","value":false,"source":"host"},)"
				R"({"id":"view.gtao.radius","type":"float","value":1.5}],)"
				R"("temporalConsumers":[{"consumer":"temporal-aa","status":"disabled","disableReason":"not-requested"},)"
				R"({"consumer":"temporal-gtao","status":"active"}],"referenceSampling":false})"));
			const auto* required = parsed.m_Request ? parsed.m_Request->m_Capture.m_RequiredFrameState.get() : nullptr;
			context.Check(required && required->m_RunIdentity == "0123456789abcdef0123456789abcdef" &&
				required->m_ContentIdentity == 481 && required->m_MinHostViewRevision == 13 &&
				required->m_Values.size() == 2 && required->m_Values[0].m_Source == HostViewValueSource::Host &&
				!std::get<bool>(required->m_Values[0].m_Value) && !required->m_Values[1].m_Source &&
				required->m_TemporalConsumers.size() == 2 &&
				required->m_TemporalConsumers[0].m_DisableReason == TemporalConsumerDisableReason::NotRequested &&
				required->m_TemporalConsumers[1].m_Status == TemporalConsumerStatus::Active &&
				!required->m_TemporalConsumers[1].m_DisableReason && required->m_ReferenceSampling == false,
				"A capture carries a typed required frame state with optional sources and reasons");
			constexpr std::string_view minimal = R"("values":[{"id":"view.taa.enabled","type":"bool","value":true}]})";
			context.Check(capture("graph", std::format("{}{}", head, minimal)).m_Request &&
				ParseApplicationControlRequest(std::format(
					R"({{"protocol":4,"id":61,"command":"export","targets":[{{"resource":"Color","at":"final"}}],"requiredFrameState":{}{}}})",
					head, minimal)).m_Request,
				"Graph and export plans accept the same predicate");
			for (const std::string_view invalid : {
				std::string_view(R"({"contentIdentity":481,"minHostViewRevision":13,"values":[{"id":"a","type":"bool","value":true}]})"),
				std::string_view(R"({"runIdentity":"0123456789ABCDEF0123456789ABCDEF","contentIdentity":481,"minHostViewRevision":13,"values":[{"id":"a","type":"bool","value":true}]})"),
				std::string_view(R"({"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":481,"minHostViewRevision":13,"values":[]})"),
				std::string_view(R"({"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":481,"minHostViewRevision":13,"values":[{"id":"a","type":"bool","value":1}]})"),
				std::string_view(R"({"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":481,"minHostViewRevision":13,"values":[{"id":"a","type":"bool","value":true,"source":"user"}]})"),
				std::string_view(R"({"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":481,"minHostViewRevision":13,"values":[{"id":"a","type":"bool","value":true,"x":1}]})"),
				std::string_view(R"({"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":481,"minHostViewRevision":13,"values":[{"id":"a","type":"bool","value":true}],"temporalConsumers":[{"consumer":"taa","status":"active"}]})"),
				std::string_view(R"({"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":481,"minHostViewRevision":13,"values":[{"id":"a","type":"bool","value":true}],"temporalConsumers":[{"consumer":"temporal-aa","status":"on"}]})"),
				std::string_view(R"({"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":481,"minHostViewRevision":13,"values":[{"id":"a","type":"bool","value":true}],"temporalConsumers":[{"consumer":"temporal-aa","status":"disabled","disableReason":"off"}]})"),
				std::string_view(R"({"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":481,"minHostViewRevision":13,"values":[{"id":"a","type":"bool","value":true}],"referenceSampling":0})"),
				std::string_view(R"({"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":481,"minHostViewRevision":13,"values":[{"id":"a","type":"bool","value":true}],"lighting":1})") })
			{
				context.Check(!capture("capture", invalid).m_Request,
					"Malformed predicates, unknown names and unknown fields reject the request");
			}

			const auto status = Json::parse(SerializeApplicationControlStatus(62, {}));
			const auto& capability = status["capabilities"]["requiredFrameState"];
			context.Check(capability["version"] == 1 && capability["commands"].size() == 3 &&
				capability["maxValues"] == 128 && capability["temporalConsumers"].size() == 3 &&
				capability["failureCodes"] == Json::array({ "RequiredFrameStateMismatch", "FrameSelectionExpired" }) &&
				capability["frameSelectionTimeoutMs"] == Json({ { "min", 1 }, { "max", 300000 }, { "default", 300000 } }) &&
				capability["admissionReasons"].size() == 2,
				"Required frame state capture is advertised with its limits and outcome codes");
			for (const std::string_view timeout : { "0", "300001", "-1", "1.5", "null", "\"100\"" })
			{
				context.Check(!ParseApplicationControlRequest(std::format(
					R"({{"protocol":4,"id":62,"command":"capture","frameSelectionTimeoutMs":{},"requiredFrameState":{}{}}})",
					timeout, head, minimal)).m_Request,
					"Invalid selection timeout values reject before admission");
			}
			const auto bounded = ParseApplicationControlRequest(std::format(
				R"({{"protocol":4,"id":62,"command":"capture","frameSelectionTimeoutMs":17,"requiredFrameState":{}{}}})",
				head, minimal));
			context.Check(bounded.m_Request &&
				bounded.m_Request->m_Capture.m_FrameSelectionTimeout == std::chrono::milliseconds(17) &&
				!ParseApplicationControlRequest(
					R"({"protocol":4,"id":62,"command":"capture","frameSelectionTimeoutMs":17})").m_Request,
				"Selection deadlines apply only to predicate-bound captures");
			const auto invalid = Json::parse(SerializeApplicationControlCaptureAdmission(63,
				{ CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::RequiredFrameStateInvalid }));
			const auto unavailable = Json::parse(SerializeApplicationControlCaptureAdmission(64,
				{ CaptureAdmissionStatus::Rejected, CaptureAdmissionReason::RequiredFrameStateUnavailable }));
			context.Check(invalid["reason"] == "RequiredFrameStateInvalid" &&
				unavailable["reason"] == "RequiredFrameStateUnavailable" && !unavailable["ok"].get<bool>(),
				"Predicate admission rejections name their reason");
			CaptureJobResult result{ .m_JobId = 9, .m_Status = CaptureJobStatus::Failed };
			const auto plain = Json::parse(SerializeApplicationControlCaptureResult(65, result, false));
			result.m_FailureCode = "RequiredFrameStateMismatch";
			const auto mismatched = Json::parse(SerializeApplicationControlCaptureResult(66, result, false));
			context.Check(plain["failureCode"].is_null() && mismatched["failureCode"] == "RequiredFrameStateMismatch",
				"A job-level failure code is reported beside the product failures");
		}
	}

	void RunApplicationControlSelfTests(SelfTestContext& context) noexcept
	{
		RunParameterControlTests(context);
		RunDeferredParameterSerializationTests(context);
		RunParseTests(context);
		RunRunIdentityCheckTests(context);
		RunHostViewOverrideTests(context);
		RunProtectedAssignmentTests(context);
		RunRequiredFrameStateTests(context);
		RunSerializeTests(context);
		RunCaptureJobOwnershipTests(context);
		RunPipeServerTests(context);
	}
}
