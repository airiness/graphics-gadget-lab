#include "RuntimePathSelfTests.h"
#include "ProductRuntimeInspection.h"
#include "GGLabRuntime/Graphics/Asset/AssetPaths.h"
#include "RuntimePaths.h"
#include "ShaderArtifactRuntime/ShaderArtifactStore.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"
#include "ShaderArtifactRuntime/VulkanShaderRuntimeABI.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

namespace gglab
{
	namespace
	{
		[[nodiscard]] constexpr ShaderTargetProfile GetTargetProfile(RHIBackendType backend) noexcept
		{
			return backend == RHIBackendType::Vulkan ?
				ShaderTargetProfile::GGLabVulkan13 :
				ShaderTargetProfile::GGLabDX12;
		}

		[[nodiscard]] constexpr ShaderArtifactCompatibilityRequest MakeCompatibilityRequest(RHIBackendType backend, ShaderStage stage) noexcept
		{
			if (backend == RHIBackendType::Vulkan)
			{
				return {
					.m_TargetProfile = ShaderTargetProfile::GGLabVulkan13,
					.m_BinaryFormat = ShaderBinaryFormat::SpirV,
					.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::Vulkan1_3,
					.m_BindingABIRevision = GGLabVulkanShaderRuntimeABI.m_Revision,
					.m_CoordinateOptions = GetGGLabVulkanShaderCoordinateOptions(stage),
					.m_Stage = stage,
				};
			}
			return {
				.m_TargetProfile = ShaderTargetProfile::GGLabDX12,
				.m_BinaryFormat = ShaderBinaryFormat::Dxil,
				.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::None,
				.m_BindingABIRevision = 0,
				.m_CoordinateOptions = ShaderCoordinateOptions::None,
				.m_Stage = stage,
			};
		}
	}

	void RunApplicationPathCompositionSelfTests(SelfTestContext& context,
		const std::filesystem::path& application) noexcept
	{
		const auto product = InspectProductRuntime(application);
		context.Check(product.m_Valid, "The actual product reports effective runtime state without GPU creation");
		if (!product.m_Valid)
		{
			return;
		}
		const auto& paths = product.m_Paths;
		context.Check(paths.IsValid() && paths.m_RuntimeRoot == std::filesystem::weakly_canonical(application.parent_path()) &&
			std::filesystem::weakly_canonical(std::filesystem::current_path()) != paths.m_RuntimeRoot,
			"Executable discovery uses the product location from an unrelated working directory");
		std::ifstream asset(ResolveAssetPath(paths.m_AssetRoot, "Textures/UVTest1K.png"), std::ios::binary);
		context.Check(asset.good() && paths.m_AssetRoot == paths.m_RuntimeRoot / "Assets",
			"Effective product asset paths open deployed content independently of CWD");
		context.Check(paths.m_ShaderArtifactRoot == paths.m_RuntimeRoot / "ShaderArtifacts" &&
			paths.m_IblDerivedDataRoot == paths.m_RuntimeRoot / "DerivedDataCache/IBL" &&
			paths.m_SettingsRoot == paths.m_RuntimeRoot,
			"Default writable paths are derived by production bootstrap");
		const auto state = std::filesystem::temp_directory_path() /
			("gglab-inspection-state-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		const auto relocated = InspectProductRuntime(application, L"--state-root \"" + state.wstring() + L"\"");
		context.Check(relocated.m_Valid && relocated.m_Paths.m_AssetRoot == paths.m_AssetRoot &&
			relocated.m_Paths.m_ShaderArtifactRoot == state / "ShaderArtifacts" &&
			relocated.m_Paths.m_SettingsRoot == state / "Settings",
			"Explicit state roots relocate writable paths while preserving deployed inputs");
		const auto conflict = InspectProductRuntime(application, L"--lab gglab.lab.culling");
		context.Check(conflict.m_Completed && conflict.m_ExitCode != 0 &&
			conflict.m_Output.find("cannot be combined") != std::string::npos,
			"Runtime inspection rejects interactive startup without creating a GPU");
		if (product.m_ArtifactOnly)
		{
			std::ifstream input(paths.m_RuntimeRoot / "artifact-only-package.json");
			const auto manifest = nlohmann::json::parse(input, nullptr, false);
			context.Check(!manifest.is_discarded() && manifest.value("backend", std::string{}) ==
				(product.m_Backend == RHIBackendType::Vulkan ? "vulkan" : "dx12"),
				"Packaged backend selection is observed in the actual product");
			const auto rejected = InspectProductRuntime(application,
				product.m_Backend == RHIBackendType::Vulkan ? L"--rhi dx12" : L"--rhi vulkan");
			context.Check(rejected.m_Completed && rejected.m_ExitCode != 0 &&
				rejected.m_Output.find("does not match") != std::string::npos,
				"The actual artifact-only product rejects a conflicting explicit backend");
		}
	}

	void RunApplicationArtifactPackageClosureSelfTests(SelfTestContext& context,
		const std::filesystem::path& application) noexcept
	{
		const auto product = InspectProductRuntime(application);
		context.Check(product.m_Valid, "Artifact closure uses effective paths and backend from the actual product");
		if (!product.m_Valid)
		{
			return;
		}
		const auto& runtimePaths = product.m_Paths;
		const auto backend = product.m_Backend;
		const ShaderTargetProfile targetProfile = GetTargetProfile(backend);
		ShaderLooseActiveProgramRegistryReader activeReader(
			ShaderLooseActiveProgramRegistryLocator(runtimePaths.m_ShaderArtifactRoot, targetProfile));
		const ActiveShaderProgramRegistryReadResult active = activeReader.Read();
		context.Check(active.IsSuccess(), "The target-scoped active registry pointer is valid");

		ShaderProgramRegistryArtifactReadResult registry{};
		if (active.IsSuccess())
		{
			ShaderLooseProgramRegistryArtifactReader registryReader(ShaderLooseProgramRegistryArtifactLocator(runtimePaths.m_ShaderArtifactRoot));
			registry = registryReader.ReadArtifact(active.m_RegistryRef);
		}
		context.Check(registry.IsSuccess(),
			"The active pointer resolves to a valid immutable program registry");

		bool closureValid = registry.IsSuccess() && !registry.m_Artifact.m_Entries.empty();
		if (closureValid)
		{
			ShaderLooseArtifactReader artifactReader(ShaderLooseArtifactLocator(runtimePaths.m_ShaderArtifactRoot));
			ShaderArtifactStore artifactStore(artifactReader);
			for (const ShaderProgramRegistryEntry& entry : registry.m_Artifact.m_Entries)
			{
				if (entry.m_TargetProfile != targetProfile ||
					!artifactStore.LoadArtifact(entry.m_ArtifactRef, MakeCompatibilityRequest(backend, entry.m_ProgramRef.m_Stage)).IsSuccess())
				{
					closureValid = false;
					break;
				}
			}
		}
		context.Check(closureValid,
			"Every registry entry closes over a present, valid, compatible artifact payload");
	}
}
