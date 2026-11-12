#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "AppRuntimeConfig.h"
#include "ApplicationContentRegistration.h"
#include "ApplicationInput.h"
#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "GGLabRuntime/Diagnostics/Snapshots/TransientResourcePoolSnapshot.h"
#include "GGLabRuntime/Graphics/Asset/AssetStreamingTypes.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadControlTypes.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadScheduling.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIResource.h"
#include "GGLabRuntime/Graphics/RenderCompositionAccess.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "GGLabRuntime/Graphics/TransferBatch.h"
#include "GGLabTestCore/SelfTest.h"
#include "GpuFaultLifecycleSelfTests.h"
#include "RuntimeIntegrationTestCases.h"
#include "RuntimeIntegrationTestServices.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gglab::runtime_test
{
	void RunScenario(SelfTestContext& checks, LifetimeProbe& probe, const std::filesystem::path& root)
	{
		FaultFactory factory(probe);
		ApplicationInput input;
		GGLabAppRuntime runtime;
		const GGLabAppRuntimeCreateInfo info{
			.m_Config = { .m_RhiBackend = AppRuntimeRHIBackend::DX12,
				.m_StartupDemoId = "FaultLifecycle.Demo", .m_InitialExtent = { 32, 32 } },
			.m_Paths = { .m_RuntimeRoot = root, .m_AssetRoot = root / "Assets",
				.m_ShaderArtifactRoot = root / "Shaders", .m_IblDerivedDataRoot = root / "IBL",
				.m_TextureDerivedDataRoot = root / "Texture", .m_EnvironmentAssetRoot = root / "Environment",
				.m_SettingsRoot = root / "Settings", .m_CaptureRoot = root / "Captures" },
			.m_HostServices = { .m_FrameCaptureImageEncoder = [&](const FrameCaptureImage&) -> std::optional<std::vector<uint8_t>>
				{
					++probe.m_ImageEncodes;
					return std::nullopt;
				} } };
		ApplicationContentRegistration content;
		content.m_Demos.push_back({ .m_Id = "FaultLifecycle.Demo", .m_Factory = &CreateFaultDemo });
		const auto registry = WriteShaderFixtures(info.m_Paths.m_ShaderArtifactRoot);
		const bool initialized = runtime.Initialize(info) == AppRuntimeInitializeResult::Succeeded &&
			runtime.InitializeServices({ .m_RHIContextFactory = &factory, .m_Input = &input,
				.m_ContentRegistration = std::move(content), .m_WindowWidth = 32, .m_WindowHeight = 32,
				.m_ShaderArtifactRoot = info.m_Paths.m_ShaderArtifactRoot, .m_ActiveShaderRegistry = registry }) == AppRuntimeServiceInitializeResult::Succeeded;
		checks.Check(initialized, "Fault lifecycle composes production AppRuntime, Renderer and artifact-only services");
		if (!initialized)
		{
			return;
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!runtime.GetShaderManager()->GetPreloadStatus().IsReady() && std::chrono::steady_clock::now() < deadline)
		{
			runtime.GetTaskSystem()->PumpCompletions();
			std::this_thread::yield();
		}
		for (uint32_t frame = 0; frame < 100 && probe.m_Readbacks == 0; ++frame)
		{
			if (runtime.Tick() != AppRuntimeTickResult::Continue)
			{
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		checks.Check(probe.m_Readbacks > 0 && probe.m_ReadbackMaps == 0,
			"Production IBL records a pending readback before the injected frame failure");
		probe.m_HoldUploads = true;
		RHIDevice& device = runtime.GetRenderHost()->GetRHIContext()->GetDevice();
		auto uploadedBuffer = std::make_shared<RHIBufferOwner>(&device, device.CreateBuffer({
			.m_SizeInBytes = 64, .m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::CopyDest }, {}));
		std::weak_ptr<RHIBufferOwner> pendingOwner = uploadedBuffer;
		auto* scheduler = runtime.GetRenderComposition()->GetAssetUploadScheduler();
		scheduler->EnqueueUploadRecording({ .m_Name = "Pending native upload" },
			[&, owner = std::move(uploadedBuffer)]()
			{
				GGLAB_UNUSED(scheduler->RecordUpload({ .m_Name = "Pending native upload" },
					[owner](TransferBatch& batch)
					{
						const std::array<uint32_t, 16> data{};
						return batch.UploadBuffer(owner->Get(), 0, data.data(), sizeof(data));
					},
					[&, owner](const AssetUploadCompletionInfo& completion)
					{
						++probe.m_UploadCallbacks;
						probe.m_UploadFailedSafely = probe.m_SafeToRelease &&
							completion.m_Status == AssetUploadStatus::Failed;
					}));
			});
		const auto capture = runtime.TrySubmitCaptureJob({
			.m_RequiredProducts = { ImageCaptureProduct{} } }).m_JobId;
		probe.m_Presentation = probe.m_ContentServices.m_RenderServices.m_Presentation;
		probe.m_EnableDeferredCommits = true;
		probe.m_Inject = true;
		checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal && probe.m_Faulted && probe.m_QuarantinedPages > 0,
			"An accepted submission without a signal quarantines actual frame uploads and fails the production runtime");
		checks.Check(probe.m_CaptureCopies > 0 && probe.m_ReadbackMaps == 0 && probe.m_EarlyDestructions == 0,
			"The retained production frame keeps graph, capture and IBL resources alive without reading GPU contents");
		checks.Check(probe.m_CommitsRun == 0 && probe.m_CommitPayloadsDestroyed == 2,
			"EndFrame CompletionUnknown drops plain CPU commits while retaining GPU-visible owners");
		TransientResourcePoolSnapshot pool;
		BuildTransientResourcePoolSnapshot(*runtime.GetRenderHost()->CreateRenderGraphCreateInfo().m_TransientResourcePool, pool);
		const bool graphRetained = std::ranges::any_of(pool.m_Textures,
			[](const TransientTextureSlotSnapshot& slot)
			{
				return slot.m_LogicalName == "FaultLifecycle.Color" && slot.m_State == TransientPoolSlotState::Leased;
			});
		checks.Check(graphRetained && probe.m_UploadCallbacks == 0 && !pendingOwner.expired(),
			"Unknown completion leaves the actual graph texture leased and the submitted asset upload owned, without retirement or publication");
		const uint32_t frames = probe.m_FrameSubmits;
		checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal &&
			runtime.GetRenderHost()->BeginFrame().IsFatal() && probe.m_FrameSubmits == frames,
			"Both AppRuntime and the production Renderer refuse another frame after CompletionUnknown");
		struct RetainedPayload
		{
			LifetimeProbe& m_Probe;
			~RetainedPayload() { m_Probe.Destroy(); }
		};
		auto payload = std::make_shared<RetainedPayload>(probe);
		std::weak_ptr<RetainedPayload> held = payload;
		runtime.GetRenderComposition()->GetAssetUploadScheduler()->EnqueueUploadRecording({ .m_Name = "Retained ready upload" },
			[&, owner = std::move(payload)]() { ++probe.m_ReadyExecutions; });
		probe.Record("before_shutdown");
		if (!checks.GetSummary().Succeeded())
		{
			probe.Record("setup_failed");
		}
		runtime.Shutdown();
		checks.Check(probe.m_ContextDestructions == 1 && probe.m_SafeToRelease && held.expired() &&
			probe.m_ReadyExecutions == 0 && probe.m_LateSubmits == 0,
			"Production shutdown cancels retained ready work only after safe quiescence and finalizes the Renderer once");
		checks.Check(probe.m_CommitsRun == 0 && probe.m_CommitPayloadsDestroyed == 2,
			"Unknown-frame callbacks never run or survive to the shutdown boundary");
		checks.Check(probe.m_EarlyDestructions == 0 && probe.m_ReadbackMaps == 0,
			"Shutdown neither destroys resources early nor maps failed frame or IBL readbacks");
		checks.Check(probe.m_UploadCallbacks == 1 && probe.m_UploadFailedSafely && pendingOwner.expired() && probe.m_ImageEncodes == 0,
			"The in-flight asset reports failure once after quiescence, releases its owner and never invokes the capture encoder");
		checks.Check(probe.m_IdleCalls == (probe.m_Outcome == TeardownOutcome::DeviceLost ? 3u : 4u) &&
			probe.m_FenceWaitsAfterFault == 0,
			"Renderer finalization verifies idle again on a live device and never waits after confirmed loss");
		bool iblCachePublished = false;
		if (std::filesystem::exists(info.m_Paths.m_IblDerivedDataRoot))
		{
			for (const auto& entry : std::filesystem::recursive_directory_iterator(info.m_Paths.m_IblDerivedDataRoot))
			{
				iblCachePublished = iblCachePublished || entry.is_regular_file();
			}
		}
		checks.Check(!iblCachePublished,
			"The pending IBL readback never publishes GPU contents to derived data");
		const auto result = runtime.TakeCaptureJobResult(capture);
		checks.Check(result && result->m_JobId == capture &&
			result->m_Status == CaptureJobStatus::Failed && result->m_Products.front().m_Files.empty(),
			"The failed production capture completes exactly once without an image");
		runtime.Shutdown();
		checks.Check(probe.m_ContextDestructions == 1 && runtime.GetLifecycleState() == AppRuntimeLifecycleState::Failed,
			"Repeated production shutdown preserves the fatal state and does not finalize twice");
	}

	void RunGpuTeardownScenarios(SelfTestContext& context,
		const std::filesystem::path& root) noexcept
	{
		for (const auto completion : { RHIFenceQueryResult::Pending, RHIFenceQueryResult::Failed,
			RHIFenceQueryResult::DeviceLost })
		{
			for (const bool resize : { false, true })
			{
				const auto name = "forward-plus-" + std::to_string(static_cast<int>(completion)) +
					(resize ? "-resize" : "-ready");
				RunForwardPlusValidationScenario(context, root / name, completion, resize, true);
			}
		}
		RunForwardPlusValidationScenario(context, root / "forward-plus-rejected",
			RHIFenceQueryResult::Failed, false, false);
		for (const auto outcome : { TeardownOutcome::Completed, TeardownOutcome::DeviceLost })
		{
			LifetimeProbe probe;
			probe.m_Outcome = outcome;
			RunScenario(context, probe, root / (outcome == TeardownOutcome::Completed ? "idle" : "loss"));
		}
		for (const std::wstring mode : { L"timeout", L"failed" })
		{
			const auto receipt = root / mode / "receipt.txt";
			std::filesystem::create_directories(receipt.parent_path());
			std::ofstream(receipt, std::ios::trunc).close();
			std::array<wchar_t, 32768> executable{};
			GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
			std::wstring command = L"\"" + std::wstring(executable.data()) + L"\" --gpu-fault-child " + mode + L" \"" + receipt.wstring() + L"\"";
			STARTUPINFOW startup{ sizeof(startup) };
			PROCESS_INFORMATION process{};
			const bool started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
				CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE;
			context.Check(started, "Unverified production teardown runs in an isolated child process");
			if (!started)
			{
				continue;
			}
			const DWORD wait = WaitForSingleObject(process.hProcess, 15000);
			if (wait != WAIT_OBJECT_0)
			{
				TerminateProcess(process.hProcess, 3);
				WaitForSingleObject(process.hProcess, 5000);
			}
			DWORD exitCode = 0;
			GetExitCodeProcess(process.hProcess, &exitCode);
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			std::ifstream file(receipt);
			const std::string events{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
			// One failed frame verification precedes Shutdown's three bounded retries.
			// Any destruction, readback, submission or unsignaled wait adds an event.
			constexpr std::string_view expectedEvents =
				"accepted_without_signal\n"
				"bounded_idle_wait\n"
				"before_shutdown\n"
				"bounded_idle_wait\n"
				"bounded_idle_wait\n"
				"bounded_idle_wait\n";
			const std::string outcomeName = mode == L"timeout" ? "Timeout" : "Failed";
			context.Check(wait == WAIT_OBJECT_0 && exitCode == EXIT_FAILURE && events == expectedEvents,
				outcomeName + " idle exhaustion exits within the bound without resource destruction, "
				"readback, new submits or waiting on an unsignaled fence");
		}
	}
}

namespace gglab
{
	using namespace runtime_test;

	void RunGpuTeardownIntegrationSelfTests(SelfTestContext& context) noexcept
	{
		RuntimeTestDirectory directory("teardown");
		const auto& root = directory.GetPath();
		RunGpuTeardownScenarios(context, root);
	}

	int RunGpuFaultLifecycleChild(std::string_view mode, const std::filesystem::path& receipt) noexcept
	{
		LifetimeProbe probe;
		probe.m_Outcome = mode == "timeout" ? TeardownOutcome::Timeout : TeardownOutcome::Failed;
		probe.m_Receipt = receipt;
		ConsoleSelfTestReporter reporter;
		SelfTestContext checks(reporter);
		RunScenario(checks, probe, receipt.parent_path() / "State");
		probe.Record("unexpected_shutdown_return");
		return 2;
	}
}
