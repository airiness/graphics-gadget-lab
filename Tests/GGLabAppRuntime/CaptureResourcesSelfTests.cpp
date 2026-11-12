#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameCaptureMetadata.h"
#include "Capture/ResourceExportWriter.h"
#include "CaptureCoordinatorTestCases.h"
#include "CapturePublicationTestFileSystem.h"
#include "CaptureTestServices.h"
#include "FrameCaptureCoordinatorSelfTests.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/Capture/ResourceExportTypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGObservation.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraphTypes.h"
#include "GGLabTestCore/SelfTest.h"

#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace gglab::capture_test
{
	void RunResourceWritingTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("resource-writing");
		FakeCaptureControl control;
		auto files = std::make_shared<CapturePublicationTestFileSystem>();
		FrameCaptureCoordinator coordinator({ .m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(), .m_WriteOnCallingThread = true,
			.m_PublicationFileSystem = files });
		CaptureTestJobs coordinatorJobs(coordinator, context);
		const auto id = coordinatorJobs.Admit({
			.m_RequiredProducts = { ResourceCaptureProduct{ { { .m_Resource = "A" },{ .m_Resource = "B" } } }, GraphCaptureProduct{} },
			.m_Label = "resource",
		});
		coordinator.BeginFrame(MakeFrameState(true));
		control.Busy(control.m_Issued.back().m_Id);
		coordinator.Update();
		context.Check(coordinatorJobs.TakeResults().empty(), "A busy group retries without publishing a partial result");
		auto state = MakeFrameState(true);
		state.m_FrameSerial = 21;
		coordinator.BeginFrame(state);
		coordinator.OnFrameSubmitted();
		control.Complete(control.m_Issued.back().m_Id);
		coordinator.Update();
		const auto results = coordinatorJobs.TakeResults();
		context.Check(results.size() == 1 && results[0].m_JobId == id && results[0].m_Status == CaptureJobStatus::Completed &&
			ProductFiles(results[0], ".npy", CaptureProductKind::Resource).size() == 2 && !ProductFile(results[0], ".json", CaptureProductKind::Graph).empty() && ProductFile(results[0], ".png").empty(),
			"The bounded writer atomically publishes every array, optional graph and one group sidecar");
		if (!results.empty() && results[0].m_Status == CaptureJobStatus::Completed)
		{
			const auto metadata = nlohmann::json::parse(ReadText(ProductFile(results[0], ".json")));
			const std::string npy = ReadText(ProductFiles(results[0], ".npy", CaptureProductKind::Resource)[0]);
			context.Check(metadata["schema"] == "gglab.resource-export" && metadata["frameSerial"] == 21 &&
				metadata["frame"]["frame"]["serial"] == 21 && metadata["targets"].size() == 2 && metadata["submission"]["completed"] == true &&
				metadata["graph"] == GetFrameCapturePathText(ProductFile(results[0], ".json", CaptureProductKind::Graph).filename()),
				"Retried groups refresh frame metadata and retain the shared fence identity");
			const uint16_t headerSize = static_cast<uint8_t>(npy[8]) | (static_cast<uint16_t>(static_cast<uint8_t>(npy[9])) << 8);
			float values[2] = {};
			std::memcpy(values, npy.data() + 10 + headerSize, sizeof(values));
			context.Check(npy.substr(1, 5) == "NUMPY" && npy[6] == 1 && npy[7] == 0 && (headerSize + 10) % 16 == 0 &&
				npy.find("'<f4'") != std::string::npos && npy.find("(1, 1, 2, 1,") != std::string::npos && values[0] == 1.25f && values[1] == -2,
				"NPY v1 headers declare the exact dtype/shape and preserve numerical bytes");
		}
		files->m_CollidePublish = files->GetPublishCount() + 2;
		GGLAB_UNUSED(coordinatorJobs.Admit({
			.m_RequiredProducts = { ResourceCaptureProduct{ { { .m_Resource = "A" },{ .m_Resource = "B" } } } },
			.m_Label = "resource",
		}));
		state.m_FrameSerial = 22;
		coordinator.BeginFrame(state);
		coordinator.OnFrameSubmitted();
		control.Complete(control.m_Issued.back().m_Id);
		coordinator.Update();
		const auto collision = coordinatorJobs.TakeResults();
		context.Check(collision.size() == 1 && collision[0].m_Status == CaptureJobStatus::Completed &&
			collision[0].m_ManifestPath.filename().string().ends_with("-2.capture-bundle.json") &&
			ReadText(files->m_CollisionPath) == "existing capture" && !HasPartialFile(directory.GetPath()),
			"A mid-group filename collision withdraws only owned files and republishes the complete group under one new stem");
		// Activity metadata discloses observers, not a successful watch claim.
		FrameCaptureMetadata observedMetadata;
		observedMetadata.m_NonFiniteScanActive = true;
		RGSnapshot observedGraph;
		observedGraph.m_Execution.m_Recorded = true;
		observedGraph.m_ObservationNodes.emplace_back(0, RGObservationTarget{
			.m_Address = { .m_Resource = "Observed" }, .m_Kind = RGObservationKind::NonFiniteScan });
		const auto graphJson = nlohmann::json::parse(SerializeFrameGraphExport(observedGraph, observedMetadata, {}, false, false));
		context.Check(graphJson["schemaVersion"] == RenderGraphExportSchemaVersion &&
			graphJson["observation"]["nonFiniteWatch"] == true &&
			graphJson["observation"]["resourceExport"] == false &&
			graphJson["observation"]["nodeKinds"] == nlohmann::json::array({ "NonFiniteScan" }) &&
			graphJson["execution"]["observationNodes"][0]["kind"] == "NonFiniteScan",
			"Scan-only graphs list their node kinds and distinguish scan activity from resource-copy observation");
		const auto resourceJson = nlohmann::json::parse(SerializeResourceExportMetadata({}, observedMetadata, {}, {}, {}));
		context.Check(resourceJson["schemaVersion"] == 2 && resourceJson["observation"]["nonFiniteWatch"] == true,
			"Resource sidecars with concurrent scans use v2 without asserting production coverage");
		ResourceExportArray structured{ .m_Type = RGResourceType::RGBuffer, .m_Shape = { 2 }, .m_Stride = 16,
			.m_Fields = { { "first", 0, ResourceExportScalar::Uint32, 1 },{ "vector", 8, ResourceExportScalar::Float32, 2 } }, .m_Data = std::vector<uint8_t>(32) };
		const auto header = MakeResourceExportNpyHeader(structured);
		const std::string text(header.begin(), header.end());
		context.Check(text.find("('', '|V4')") != std::string::npos && text.find("'vector', '<f4', (2,)") != std::string::npos,
			"Structured GPU dtypes preserve field offsets, padding, vector counts and stride");
		structured.m_Data.pop_back();
		bool rejected = false;
		try { GGLAB_UNUSED(MakeResourceExportNpyHeader(structured)); }
		catch (const std::exception&)
		{
			rejected = true;
		}
		context.Check(rejected, "Mismatched array shape/stride/data fails before file publication");
		coordinator.FinalizeAfterRenderHost();
	}

	void RunGraphWritingTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("graph-writing");
		FakeCaptureControl control;
		FrameCaptureCoordinator coordinator({ .m_Capture = &control,
			.m_DefaultOutputDirectory = directory.GetPath(), .m_WriteOnCallingThread = true });
		CaptureTestJobs coordinatorJobs(coordinator, context);
		const auto id = coordinatorJobs.Admit({
			.m_RequiredProducts = { GraphCaptureProduct{ false } },
			.m_Label = "graph",
		});
		const auto summaryId = coordinatorJobs.Admit({
			.m_RequiredProducts = { GraphCaptureProduct{ true } },
			.m_Label = "graph",
		});
		coordinator.BeginFrame(MakeFrameState(true));
		coordinator.OnFrameSubmitted();
		for (const auto& issued : control.m_Issued)
		{
			control.Complete(issued.m_Id);
		}
		coordinator.Update();
		std::vector<CaptureJobResult> results;
		for (CaptureJobResult& ready : coordinatorJobs.TakeResults())
		{
			results.push_back(std::move(ready));
		}
		context.Check(results.size() == 2 && results[0].m_JobId == id && results[1].m_JobId == summaryId &&
			results[0].m_Status == CaptureJobStatus::Completed &&
			results[1].m_Status == CaptureJobStatus::Completed && ProductFile(results[0], ".png").empty(),
			"Full and summary graph requests reuse the bounded writer without requiring a PNG encoder");
		if (results.size() == 2 && results[0].m_Status == CaptureJobStatus::Completed &&
			results[1].m_Status == CaptureJobStatus::Completed)
		{
			const auto full = nlohmann::json::parse(ReadText(ProductFile(results[0], ".json")));
			const auto summary = nlohmann::json::parse(ReadText(ProductFile(results[1], ".json")));
			context.Check(full["schema"] == "gglab.render-graph" && full["frameSerial"] == 17 &&
				full["frame"]["frame"]["serial"] == 17 && full["submission"]["fenceValue"] == 19 &&
				full["submission"]["completed"] == true && full["observation"]["graph"] == true &&
				full["frame"]["adapterDriverIdentity"] == "Capture.TestAdapter" &&
				full["observation"]["nodeKinds"].empty() && summary["observation"]["nodeKinds"].empty() &&
				summary["mode"] == "summary" && !summary.contains("execution"),
				"Graph JSON embeds the same frame block and completed submission identity in both modes");
		}
		coordinator.FinalizeAfterRenderHost();
	}

	void RunGraphFailureTests(SelfTestContext& context) noexcept
	{
		TemporaryDirectory directory("graph-failures");
		{
			FakeCaptureControl control;
			control.m_RequestAvailability = FrameCaptureAvailability::Busy;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(), .m_WriteOnCallingThread = true });
			CaptureTestJobs coordinatorJobs(coordinator, context);
			GGLAB_UNUSED(coordinatorJobs.Admit({ .m_RequiredProducts = { GraphCaptureProduct{ false } } }));
			coordinator.BeginFrame(MakeFrameState(true));
			const auto results = coordinatorJobs.TakeResults();
			context.Check(coordinator.GetCaptureAvailability({ .m_RequiredProducts = { GraphCaptureProduct{} } }) ==
				FrameCaptureAvailability::Busy && control.m_Issued.empty() && results.size() == 1 &&
				results[0].m_Status == CaptureJobStatus::Failed,
				"Graph coordination respects Runtime request-only admission before issuing");
			coordinator.FinalizeAfterRenderHost();
		}
		{
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = directory.GetPath(), .m_WriteOnCallingThread = true });
			CaptureTestJobs coordinatorJobs(coordinator, context);
			GGLAB_UNUSED(coordinatorJobs.Admit({
				.m_RequiredProducts = { GraphCaptureProduct{ false } },
				.m_Note = std::string(1, '\xFF'),
			}));
			coordinator.BeginFrame(MakeFrameState(true));
			control.Complete(control.m_Issued[0].m_Id);
			coordinator.Update();
			const auto results = coordinatorJobs.TakeResults();
			std::error_code error;
			context.Check(results.size() == 1 && results[0].m_Status == CaptureJobStatus::Failed &&
				results[0].m_Products.front().m_Failure.find("Encoding the capture bundle failed") != std::string::npos &&
				!std::filesystem::exists(directory.GetPath(), error),
				"Invalid UTF-8 graph metadata fails safely before writing or publishing files");
			coordinator.FinalizeAfterRenderHost();
		}
		std::error_code error;
		std::filesystem::create_directories(directory.GetPath(), error);
		const auto blocker = directory.GetPath() / std::filesystem::path(u8"\u4e2d\u6587 \U0001F9EA blocker");
		{
			std::ofstream file(blocker, std::ios::binary);
			file << "not a directory";
		}
		for (const auto& product : std::vector<CaptureProductSpec>{ ImageCaptureProduct{}, GraphCaptureProduct{} })
		{
			FakeCaptureControl control;
			FrameCaptureCoordinator coordinator({ .m_Capture = &control,
				.m_DefaultOutputDirectory = blocker / "output", .m_ImageEncoder = &EncodeTestPng,
				.m_WriteOnCallingThread = true });
			CaptureTestJobs coordinatorJobs(coordinator, context);
			GGLAB_UNUSED(coordinatorJobs.Admit({ .m_RequiredProducts = { product } }));
			coordinator.BeginFrame(MakeFrameState(true));
			control.Complete(control.m_Issued[0].m_Id);
			coordinator.Update();
			const auto results = coordinatorJobs.TakeResults();
			context.Check(results.size() == 1 && results[0].m_Status == CaptureJobStatus::Failed &&
				results[0].m_Products.front().m_Failure.find(GetFrameCapturePathText(blocker)) != std::string::npos &&
				ProductFile(results[0], ".json").empty() && ProductFile(results[0], ".png").empty(),
				"Image and graph write failures report Unicode paths as UTF-8 without code-page exceptions");
			coordinator.FinalizeAfterRenderHost();
		}
	}
}

namespace gglab
{
	using namespace capture_test;

	void RunCaptureResourcesSelfTests(SelfTestContext& context) noexcept
	{
		RunResourceWritingTests(context);
		RunGraphWritingTests(context);
		RunGraphFailureTests(context);
	}
}
