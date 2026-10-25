#include "DevelopmentShaderHotReloadSelfTests.h"
#include "Application/Shader/DevelopmentShaderBuildBridge.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace gglab
{
	struct DevelopmentShaderHotReloadTestAccess
	{
		static void SetPending(DevelopmentShaderHotReloadSystem& bridge,
			ShaderProgramRegistryArtifactRef registry) noexcept
		{
			bridge.m_PendingRegistry = registry;
		}
		static bool HasPending(const DevelopmentShaderHotReloadSystem& bridge) noexcept
		{
			return bridge.m_PendingRegistry.has_value();
		}
		static bool IsStopped(const DevelopmentShaderHotReloadSystem& bridge) noexcept
		{
			return bridge.m_ShuttingDown && !bridge.m_RenderServices.IsValid();
		}
		static void RequestBuild(DevelopmentShaderHotReloadSystem& bridge) noexcept
		{
			bridge.m_RebuildRequested = true;
			bridge.m_LastObservedChange = {};
		}
		static void QueueCompletion(DevelopmentShaderHotReloadSystem& bridge, TaskSystem& tasks,
			ShaderProgramRegistryArtifactRef registry, bool& consumed) noexcept
		{
			// Exercise the production completion path with an owned build result;
			// external process execution is covered by the process-client suite.
			bridge.m_BuildTask = tasks.Submit({},
				[](std::stop_token) noexcept { return TaskResult::Success(); },
				[&bridge, registry, &consumed](const TaskCompletionInfo& info) noexcept
				{
					bridge.OnBuildCompleted(info, {
						.m_Status = DevelopmentShaderBuildStatus::Succeeded, .m_RegistryRef = registry });
					consumed = true;
				});
		}
	};

	namespace
	{
		class TemporalProbe final : public RenderTemporalAccess
		{
		public:
			void InvalidateTemporalHistoryAfterResolveProgramChange() noexcept override { ++m_Invalidations; }
			TemporalHistoryManagerDiagnostics GetTemporalHistoryDiagnostics() const override { return {}; }
			uint32_t m_Invalidations = 0;
		};

		bool WriteBytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
		{
			std::filesystem::create_directories(path.parent_path());
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			return stream.good();
		}

		ShaderArtifactRef WriteArtifact(const std::filesystem::path& root, uint8_t marker)
		{
			ShaderRuntimeArtifact artifact;
			artifact.m_Manifest.m_BinaryFormat = ShaderBinaryFormat::Dxil;
			artifact.m_Manifest.m_Stage = ShaderStage::Compute;
			artifact.m_Manifest.m_EntryPoint = "CSMain";
			artifact.m_Binary = ShaderBinary(20);
			std::memset(artifact.m_Binary.Data(), 0, 20);
			std::memcpy(artifact.m_Binary.Data(), "DXBC", 4);
			static_cast<std::byte*>(artifact.m_Binary.Data())[19] = static_cast<std::byte>(marker);
			const auto bytes = std::span(static_cast<const std::byte*>(artifact.m_Binary.Data()), 20);
			artifact.m_Manifest.m_BinaryContentDigest.m_Digest = ComputeSha256(bytes);
			artifact.m_Manifest.m_ArtifactId = ComputeShaderArtifactId(artifact.m_Manifest);
			const ShaderArtifactRef ref{ .m_ArtifactId = artifact.m_Manifest.m_ArtifactId };
			const auto paths = ShaderLooseArtifactLocator(root).GetPaths(ref);
			return WriteBytes(paths.m_BinaryPath, bytes) &&
				WriteBytes(paths.m_ManifestPath, SerializeShaderRuntimeArtifactManifest(artifact.m_Manifest)) ?
				ref : ShaderArtifactRef{};
		}

		ShaderProgramRegistryArtifactRef WriteRegistry(const std::filesystem::path& root,
			uint8_t resolveMarker, uint8_t otherMarker)
		{
			const std::array entries
			{
				ShaderProgramRegistryEntry{ .m_ProgramRef = shader_programs::TemporalAAReprojectionCompute,
					.m_ArtifactRef = WriteArtifact(root, resolveMarker) },
				ShaderProgramRegistryEntry{ .m_ProgramRef = shader_programs::RenderGraphComputeWrite,
					.m_ArtifactRef = WriteArtifact(root, otherMarker) },
				ShaderProgramRegistryEntry{ .m_ProgramRef = shader_programs::RenderGraphComputeReadWrite,
					.m_ArtifactRef = WriteArtifact(root, 6) },
				ShaderProgramRegistryEntry{ .m_ProgramRef = shader_programs::TemporalAADepthHistoryCompute,
					.m_ArtifactRef = WriteArtifact(root, 7) },
			};
			const auto built = BuildShaderProgramRegistryArtifact(entries);
			const ShaderProgramRegistryArtifactRef ref{ .m_RegistryId = built.m_Artifact.m_RegistryId };
			const auto path = ShaderLooseProgramRegistryArtifactLocator(root).GetPath(ref);
			return built.IsSuccess() && WriteBytes(path.m_Path,
				SerializeShaderProgramRegistryArtifact(built.m_Artifact)) ? ref : ShaderProgramRegistryArtifactRef{};
		}

		RenderServices Bindings(ShaderManager& manager, TemporalProbe& temporal) noexcept
		{
			// Only shader and temporal capabilities are invoked by the production bridge.
			return {
				.m_PipelineResolver = reinterpret_cast<RenderPipelineResolver*>(uintptr_t{ 1 }),
				.m_ShaderPrograms = &manager,
				.m_Samplers = reinterpret_cast<RenderSamplerAccess*>(uintptr_t{ 1 }),
				.m_Resources = reinterpret_cast<RenderResourceRegistryAccess*>(uintptr_t{ 1 }),
				.m_FrameBuffers = reinterpret_cast<RenderFrameBufferAccess*>(uintptr_t{ 1 }),
				.m_Environment = reinterpret_cast<RenderEnvironmentAccess*>(uintptr_t{ 1 }),
				.m_Presentation = reinterpret_cast<RenderPresentationAccess*>(uintptr_t{ 1 }),
				.m_BindingLayout = reinterpret_cast<RenderBindingLayoutAccess*>(uintptr_t{ 1 }),
				.m_Temporal = &temporal,
				.m_AssetUpload = reinterpret_cast<AssetUploadScheduling*>(uintptr_t{ 1 }),
			};
		}

		template <typename Predicate>
		bool PumpUntil(TaskSystem& tasks, Predicate&& predicate)
		{
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!predicate() && std::chrono::steady_clock::now() < deadline)
			{
				tasks.PumpCompletions();
				std::this_thread::yield();
			}
			return predicate();
		}
	}

	void RunDevelopmentShaderHotReloadSelfTests(SelfTestContext& context) noexcept
	{
		using Access = DevelopmentShaderHotReloadTestAccess;
		const auto root = std::filesystem::temp_directory_path() /
			("gglab-hot-reload-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		const auto artifacts = root / "Artifacts";
		const auto initial = WriteRegistry(artifacts, 1, 2);
		const auto resolveChanged = WriteRegistry(artifacts, 3, 2);
		const auto unrelatedChanged = WriteRegistry(artifacts, 3, 4);
		const auto laterResolve = WriteRegistry(artifacts, 5, 4);
		std::filesystem::create_directories(root / "Sources");
		const DevelopmentShaderBuildRequest request{
			.m_ActiveBackend = RHIBackendType::DX12, .m_ShaderCompilerPath = root / "gglab-shaderc.exe",
			.m_ShaderSourceRoot = root / "Sources", .m_ShaderCacheRoot = root / "Cache",
			.m_ArtifactRoot = artifacts };
		auto tasks = std::make_unique<TaskSystem>();
		auto manager = std::make_unique<ShaderManager>(ShaderManagerCreateInfo{
			.m_ActiveBackend = RHIBackendType::DX12, .m_ArtifactRoot = artifacts, .m_ActiveRegistry = initial });
		TemporalProbe temporal;
		RenderServicesPublication publication(Bindings(*manager, temporal));
		auto bridge = std::make_unique<DevelopmentShaderHotReloadSystem>(
			DevelopmentShaderHotReloadSystem::CreateInfo{ .m_BuildRequest = request,
				.m_TaskSystem = tasks.get(), .m_ShaderManager = manager.get(), .m_RenderServices = publication.GetView() });
		const bool initialized = initial.IsValid() && resolveChanged.IsValid() && unrelatedChanged.IsValid() &&
			laterResolve.IsValid() && manager->IsReady() && bridge->Initialize();
		context.Check(initialized, "Production Hot Reload initializes through a retained service view");
		if (!initialized)
		{
			return;
		}
		const auto resolve = manager->LoadProgram(shader_programs::TemporalAAReprojectionCompute);
		const auto other = manager->LoadProgram(shader_programs::RenderGraphComputeWrite);
		context.Check(resolve.IsValid() && other.IsValid(), "Hot Reload fixtures load both registry programs");
		const auto generation = manager->GetGeneration(resolve);
		{
			auto incomplete = Bindings(*manager, temporal);
			incomplete.m_Temporal = nullptr;
			RenderServicesPublication unavailable(incomplete);
			DevelopmentShaderHotReloadSystem rejected({ .m_BuildRequest = request,
				.m_TaskSystem = tasks.get(), .m_ShaderManager = manager.get(), .m_RenderServices = unavailable.GetView() });
			context.Check(!rejected.Initialize(), "Hot Reload initialization requires usable temporal services");
		}
		// Already loaded programs take the synchronous fast path; keep a distinct
		// unloaded program for each Busy window so completion pumping controls it.
		const auto preload = manager->RequestPreloadAsync(*tasks, { shader_programs::RenderGraphComputeReadWrite });
		Access::SetPending(*bridge, resolveChanged);
		bridge->Update();
		context.Check(Access::HasPending(*bridge) && manager->GetActiveRegistryRef() == initial &&
			temporal.m_Invalidations == 0, "Busy activation retains its pending registry without invalidation");
		context.Check(PumpUntil(*tasks, [&]() { return !preload.GetStatus().IsPreparing(); }),
			"Preload completion is consumed before retrying Hot Reload activation");
		bridge->Update();
		context.Check(!Access::HasPending(*bridge) && manager->GetActiveRegistryRef() == resolveChanged &&
			manager->GetGeneration(resolve) == generation + 1 && temporal.m_Invalidations == 1,
			"Activated resolve changes invalidate temporal history exactly once inside the borrow");
		Access::SetPending(*bridge, resolveChanged);
		bridge->Update();
		context.Check(!Access::HasPending(*bridge) && temporal.m_Invalidations == 1,
			"AlreadyActive registry activation does not invalidate history");
		bool consumed = false;
		Access::QueueCompletion(*bridge, *tasks, unrelatedChanged, consumed);
		context.Check(PumpUntil(*tasks, [&]() { return consumed; }) &&
			manager->GetActiveRegistryRef() == unrelatedChanged && manager->GetGeneration(resolve) == generation + 1 &&
			temporal.m_Invalidations == 1,
			"The production build-completion path activates unrelated changes without resolve invalidation");
		auto missing = laterResolve;
		missing.m_RegistryId.m_DurableDigest.m_Value[0] ^= std::byte{ 0x40 };
		Access::SetPending(*bridge, missing);
		bridge->Update();
		context.Check(!Access::HasPending(*bridge) && temporal.m_Invalidations == 1 &&
			manager->GetActiveRegistryRef() == unrelatedChanged,
			"Rejected registry activation preserves current shaders and temporal history");
		const auto blocked = manager->RequestPreloadAsync(*tasks, { shader_programs::TemporalAADepthHistoryCompute });
		Access::SetPending(*bridge, laterResolve);
		bridge->Update();
		context.Check(Access::HasPending(*bridge), "A second Busy registry remains pending before revocation");
		publication.Revoke();
		const auto submissions = tasks->GetStatistics().m_SubmittedCount;
		Access::RequestBuild(*bridge);
		bridge->Update();
		context.Check(Access::IsStopped(*bridge) && !Access::HasPending(*bridge) &&
			tasks->GetStatistics().m_SubmittedCount == submissions && temporal.m_Invalidations == 1 &&
			manager->GetActiveRegistryRef() == unrelatedChanged,
			"Revocation stops pending activation and new build admission before touching services");
		context.Check(PumpUntil(*tasks, [&]() { return !blocked.GetStatus().IsPreparing(); }),
			"Independent preload completion still drains after Hot Reload loses publication");

		RenderServicesPublication nextPublication(Bindings(*manager, temporal));
		bridge = std::make_unique<DevelopmentShaderHotReloadSystem>(
			DevelopmentShaderHotReloadSystem::CreateInfo{ .m_BuildRequest = request,
				.m_TaskSystem = tasks.get(), .m_ShaderManager = manager.get(), .m_RenderServices = nextPublication.GetView() });
		context.Check(bridge->Initialize(), "A new host publication initializes a separate Hot Reload consumer");
		consumed = false;
		Access::QueueCompletion(*bridge, *tasks, laterResolve, consumed);
		nextPublication.Revoke();
		context.Check(PumpUntil(*tasks, [&]() { return consumed; }) && Access::IsStopped(*bridge) &&
			manager->GetActiveRegistryRef() == unrelatedChanged && temporal.m_Invalidations == 1,
			"A build completion observes revoked publication before registry or temporal access");
		RenderServicesPublication finalPublication(Bindings(*manager, temporal));
		bridge = std::make_unique<DevelopmentShaderHotReloadSystem>(
			DevelopmentShaderHotReloadSystem::CreateInfo{ .m_BuildRequest = request,
				.m_TaskSystem = tasks.get(), .m_ShaderManager = manager.get(), .m_RenderServices = finalPublication.GetView() });
		context.Check(bridge->Initialize(), "The shutdown fixture initializes before completion draining");
		consumed = false;
		Access::QueueCompletion(*bridge, *tasks, laterResolve, consumed);
		bridge->Shutdown();
		tasks->Shutdown();
		tasks->PumpCompletions();
		context.Check(consumed && manager->GetActiveRegistryRef() == unrelatedChanged &&
			temporal.m_Invalidations == 1,
			"Queued build completion drains with its stopped owner alive and cannot activate");
		finalPublication.Revoke();
		manager.reset();
		tasks.reset();
		bridge.reset();
		context.Check(temporal.m_Invalidations == 1,
			"Hot Reload destruction after provider teardown consults no dead manager or scheduler");
		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
	}
}
