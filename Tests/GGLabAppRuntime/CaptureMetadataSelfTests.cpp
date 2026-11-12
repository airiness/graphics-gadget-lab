#include "Capture/FrameCaptureMetadata.h"
#include "Capture/FrameCaptureReadiness.h"
#include "CaptureCoordinatorTestCases.h"
#include "FrameCaptureCoordinatorSelfTests.h"
#include "GGLabRuntime/Graphics/Pipeline/GIFieldObservation.h"
#include "GGLabRuntime/Graphics/Pipeline/GIFieldProfile.h"
#include "GGLabRuntime/Graphics/Pipeline/GIFieldReadiness.h"
#include "GGLabRuntime/Graphics/Pipeline/GIProbeVisualization.h"
#include "GGLabTestCore/SelfTest.h"
#include "Lab/LabParameter.h"
#include "Parameters/ParameterEffectReceipt.h"
#include "Parameters/ParameterFrameState.h"

#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace gglab::capture_test
{
	void RunMetadataSerializationTests(SelfTestContext& context) noexcept
	{
		FrameCaptureMetadata metadata{
			.m_RequestId = 7,
			.m_Note = "line\nbreak\ttab\\slash\x01",
			.m_FixedDeltaTime = std::nullopt,
			.m_TotalTime = std::numeric_limits<double>::infinity(),
		};
		metadata.m_Readiness.Add("ibl", FrameCaptureGateState::Pending, "Baking.");
		metadata.m_Readiness.m_GIField = GIFieldCaptureState{
			.m_Generation = 73, .m_SceneRevision = 19, .m_LightingGeneration = 11,
			.m_EnvironmentGeneration = 7,
			.m_Readiness = { .m_State = GIFieldReadinessState::Ready, .m_Enabled = true,
				.m_Refreshing = false, .m_CompletedUpdates = 84, .m_ReadyUpdate = 54 } };
		const auto fieldJson = nlohmann::json::parse(SerializeFrameCaptureMetadata(metadata));
		context.Check(fieldJson["readiness"]["giField"]["schemaVersion"] == 1 &&
			fieldJson["readiness"]["giField"]["generation"] == 73 &&
			fieldJson["readiness"]["giField"]["completedUpdates"] == 84 &&
			fieldJson["readiness"]["giField"]["state"] == "Ready",
			"Field readiness and generation are serialized from the frozen frame snapshot");
		auto measurement = std::make_shared<GIFieldReadinessMeasurement>();
		measurement->m_Counts = { 4, 4, 4 };
		measurement->m_BlockWidth = 4;
		measurement->m_RequiredQuietUpdates = 30;
		measurement->m_Blocks.push_back({});
		auto change = std::make_shared<GIFieldChangeObservation>();
		change->m_Observation = { .m_Field = { .m_Generation = 73, .m_RefreshEpoch = 2,
			.m_EpochOriginUpdate = 80 }, .m_ProducingFrame = 113, .m_FieldUpdate = 85, .m_ReadinessUpdate = 5 };
		change->m_Measurement = measurement;
		GIFieldSummary summary{ .m_ChangeObservation = change };
		summary.m_Visualization.m_ChangeObservation = change;
		summary.m_Visualization.m_Settings.m_Mode = GIProbeVisualizationMode::Change;
		const auto changeJson = nlohmann::json::parse(SerializeGIFieldSummary(summary));
		context.Check(changeJson["changeObservation"] == changeJson["probeVisualization"]["changeObservation"] &&
			changeJson["changeObservation"]["blocks"][0]["p95"].is_null() &&
			changeJson["changeObservation"]["blocks"][0]["excluded"] == true &&
			changeJson["changeObservation"]["observation"]["readinessUpdate"] == 5 &&
			changeJson["probeVisualization"]["mode"] == "change" &&
			changeJson["probeVisualization"]["schemaVersion"] == 7,
			"Change metadata shares the completed observation and preserves Excluded/null semantics");
		context.Check(changeJson["readinessTrace"].is_null(),
			"A field without an opt-in trace reports no readiness history");
		auto first = std::make_shared<GIFieldReadinessTraceUpdate>();
		first->m_Readiness = { .m_State = GIFieldReadinessState::Warming, .m_CompletedUpdates = 4 };
		first->m_RefreshMode = true;
		first->m_Blocks.push_back({ .m_ActiveProbes = 64, .m_Percentile95 = 0.5f, .m_Maximum = 0.75f,
			.m_Blockers = 15 });
		auto latest = std::make_shared<GIFieldReadinessTraceUpdate>();
		latest->m_Readiness = { .m_State = GIFieldReadinessState::Ready, .m_CompletedUpdates = 5,
			.m_ReadyUpdate = 5, .m_Threshold = 0.25f };
		latest->m_Blocks.push_back({});
		latest->m_Previous = first;
		measurement->m_Trace = latest;
		const auto traceJson = nlohmann::json::parse(SerializeGIFieldSummary(summary))["readinessTrace"];
		context.Check(traceJson["schemaVersion"] == 1 && traceJson["capacity"] == 240 &&
			traceJson["field"]["refreshEpoch"] == 2 && traceJson["updates"].size() == 2 &&
			traceJson["updates"][0]["update"] == 4 && traceJson["updates"][0]["phase"] == "refresh" &&
			traceJson["updates"][0]["p95"][0] == 0.5f && traceJson["updates"][0]["blockers"][0] == 15 &&
			traceJson["updates"][1]["state"] == "Ready" && traceJson["updates"][1]["phase"] == "steady" &&
			traceJson["updates"][1]["readyUpdate"] == 5 && traceJson["updates"][1]["threshold"] == 0.25f &&
			traceJson["updates"][1]["p95"][0].is_null() && traceJson["updates"][1]["activeProbes"][0] == 0,
			"A readiness trace lists its updates oldest first with per-block decisions and null statistics");
		measurement->m_Trace.reset();
		context.Check(changeJson["profile"].is_null(),
			"Absent profile receipts remain unavailable instead of inventing a qualified default");
		auto profile = std::make_shared<GIFieldProfileReceipt>();
		profile->m_Volume.m_Profile = { "gglab.gi.field.contract", 3 };
		profile->m_RaysPerProbe = 192;
		profile->m_Seed = 81052;
		profile->m_IrradianceInteriorWidth = 14;
		profile->m_MaximumDistance = 1000;
		profile->m_Hysteresis = 0.9f;
		profile->m_ViewBias = 0.15f;
		profile->m_EnableFeedback = true;
		profile->m_ProgressiveSampling = true;
		profile->m_Readiness.m_HardReset = { .m_RefreshSigma = 0.02f, .m_SteadySigma = 0.03f };
		profile->m_Readiness.m_Lighting = { .m_RefreshSigma = 0.25f, .m_SteadySigma = 0.04f };
		profile->m_Readiness.m_LightingIntegration = GIFieldRefreshIntegration::Progressive;
		profile->m_ConstantEnvironment = true;
		profile->m_ConstantRadiance = { 1, 0.5f, 2 };
		summary.m_Profile = profile;
		const auto profileJson = nlohmann::json::parse(SerializeGIFieldSummary(summary))["profile"];
		const auto& readinessJson = profileJson["readiness"];
		context.Check(profileJson["schemaVersion"] == 2 &&
			profileJson["id"] == "gglab.gi.field.contract" && profileJson["revision"] == 3 &&
			profileJson["counts"] == profile->m_Volume.m_Counts &&
			profileJson["minimum"] == profile->m_Volume.m_Minimum && profileJson["spacing"] == 1 &&
			profileJson["raysPerProbe"] == 192 && profileJson["seed"] == 81052 &&
			profileJson["irradianceInteriorWidth"] == 14 && profileJson["maximumDistance"] == 1000 &&
			profileJson["hysteresis"] == profile->m_Hysteresis &&
			profileJson["viewBias"] == profile->m_ViewBias && !profileJson["receiverSampling"].get<bool>() &&
			profileJson["feedback"] == true && profileJson["progressiveSampling"] == true &&
			profileJson["constantEnvironment"] == true &&
			profileJson["constantRadiance"] == profile->m_ConstantRadiance &&
			readinessJson.size() == 4 && readinessJson["enabled"] == true &&
			readinessJson["refreshHysteresis"] == 0.5 &&
			readinessJson["hardReset"] == nlohmann::json{ { "integration", "progressive" },
				{ "refreshSigma", 0.02f }, { "steadySigma", 0.03f } } &&
			readinessJson["lighting"] == nlohmann::json{ { "integration", "progressive" },
				{ "refreshSigma", 0.25f }, { "steadySigma", 0.04f } },
			"Versioned profile metadata retains base identity and every effective constructor input");
		metadata.m_Readiness.m_GIField->m_Summary = std::make_shared<GIFieldSummary>(summary);
		context.Check(nlohmann::json::parse(SerializeFrameCaptureMetadata(metadata))
			["readiness"]["giField"]["summary"]["profile"] == profileJson,
			"Frame metadata embeds the same retained profile receipt without relabeling it");

		const std::string json = SerializeFrameCaptureMetadata(metadata);
		context.Check(
			json.find("\"note\": \"line\\nbreak\\ttab\\\\slash\\u0001\"") != std::string::npos &&
			json.find("\"fixedDeltaTime\": null") != std::string::npos &&
			json.find("\"totalTime\": null") != std::string::npos &&
			json.find("\"state\": \"pending\"") != std::string::npos,
			"Metadata JSON escapes control characters and writes non-finite numbers as null");

		metadata.m_Temporal = {
			.m_Requested = true,
			.m_Status = "active",
			.m_DisableReason = "none",
			.m_Consumers = {
				{
					.m_Name = "temporal-aa",
					.m_Requested = true,
					.m_Status = "active",
					.m_DisableReason = "none",
					.m_Services = { "projection-jitter", "geometry-motion" },
				},
				{
					.m_Name = "reference",
					.m_Status = "disabled",
					.m_DisableReason = "not-requested",
				},
			},
			.m_Services = { "projection-jitter", "geometry-motion" },
			.m_SessionIdentity = 5,
			.m_ResetIdentity = 9,
			.m_JitterIndex = 3,
			.m_JitterSequenceLength = 8,
			.m_JitterPixels = { -0.375f, -0.0625f },
			.m_MaxHistoryFeedback = 0.97f,
			.m_RenderExtent = { 640, 360 },
			.m_DisplayExtent = { 1280, 720 },
			.m_RenderScale = 0.5f,
		};
		metadata.m_Sequence = FrameCaptureSequenceInfo{
			.m_SequenceId = 2,
			.m_CameraPathId = "SEQ_Test",
			.m_CameraPathVersion = 4,
			.m_Frame = 17,
			.m_FrameCount = 96,
		};
		const std::string sequenceJson = SerializeFrameCaptureMetadata(metadata);
		context.Check(
			sequenceJson.find("\"status\": \"active\"") != std::string::npos &&
			sequenceJson.find("\"jitterIndex\": 3") != std::string::npos &&
			sequenceJson.find("\"jitterSequenceLength\": 8") != std::string::npos &&
			sequenceJson.find("-0.375") != std::string::npos &&
			sequenceJson.find("\"maxHistoryFeedback\": 0.97") != std::string::npos &&
			sequenceJson.find("\"name\": \"temporal-aa\"") != std::string::npos &&
			sequenceJson.find("\"name\": \"reference\"") != std::string::npos &&
			sequenceJson.find("\"disableReason\": \"not-requested\"") != std::string::npos &&
			sequenceJson.find("\"geometry-motion\"") != std::string::npos &&
			sequenceJson.find("\"renderScale\": 0.5") != std::string::npos &&
			sequenceJson.find("\"cameraPath\": \"SEQ_Test\"") != std::string::npos &&
			sequenceJson.find("\"cameraPathVersion\": 4") != std::string::npos &&
			sequenceJson.find("\"frame\": 17") != std::string::npos &&
			json.find("\"sequence\": null") != std::string::npos,
			"Metadata records temporal consumers, services, jitter, settings and render "
			"scale, and the sequence frame when the capture belongs to a sequence");

		// Failure text can carry host-encoded paths or system messages. Frozen
		// serialization is noexcept and must replace invalid UTF-8 instead of throwing.
		ParameterFrameState parameters{ .m_FrameSerial = 11, .m_Revision = 3,
			.m_ContentId = "gglab.lab.fixture" };
		parameters.m_Receipts.push_back({ .m_BatchId = 4, .m_Item = {
			.m_Parameter = LabParameterId("fixture.samples"),
			.m_DeclaredClass = ParameterEffectClass::Rebuild,
			.m_State = ParameterEffectState::Failed,
			.m_Requested = 10,
			.m_Failure = "Failed to prepare lab: \xC0\xFF" } });
		metadata.m_Parameters = std::make_shared<const ParameterFrameState>(parameters);
		const auto frozen = nlohmann::json::parse(SerializeParameterFrameState(parameters), nullptr, false);
		const auto embedded = nlohmann::json::parse(SerializeFrameCaptureMetadata(metadata), nullptr, false);
		context.Check(!frozen.is_discarded() && !embedded.is_discarded() &&
			frozen["receipts"][0]["failure"].get<std::string>().find("\xEF\xBF\xBD") != std::string::npos &&
			embedded["parameters"] == frozen,
			"Frozen parameter failure text replaces invalid UTF-8 instead of terminating serialization");
	}
}

namespace gglab
{
	using namespace capture_test;

	void RunFrameCaptureMetadataSelfTests(SelfTestContext& context) noexcept
	{
		RunMetadataSerializationTests(context);
	}
}
