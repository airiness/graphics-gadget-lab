#include "RuntimePathSelfTests.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Asset/AssetPaths.h"
#include "RuntimePaths.h"
#include "ShaderArtifactRuntime/ShaderArtifactStore.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"
#include "ShaderArtifactRuntime/VulkanShaderRuntimeABI.h"

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

		[[nodiscard]] constexpr std::string_view GetTargetProfileName(RHIBackendType backend) noexcept
		{
			return backend == RHIBackendType::Vulkan ? "gglab-vulkan13" : "gglab-dx12";
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
		const RuntimePaths& runtimePaths, RHIBackendType backend) noexcept
	{
		std::error_code error;
		const std::filesystem::path currentDirectory = std::filesystem::current_path(error);
		context.Check(!error && currentDirectory.lexically_normal() != runtimePaths.m_RuntimeRoot.lexically_normal(),
			"Path composition proof runs outside the executable runtime root");
		context.Check(runtimePaths.IsValid(),
			"Executable discovery produces valid absolute runtime paths");

		const std::filesystem::path assetPath = ResolveAssetPath(runtimePaths.m_AssetRoot, "Textures/UVTest1K.png");
		std::ifstream assetStream(assetPath, std::ios::binary);
		context.Check(assetStream.good(),
			"Injected asset root opens packaged content independently of process CWD");

#if defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
		// A package must already contain its immutable active registry. Development
		// publication is intentionally deferred until Application initialization,
		// which this early-exit path-composition proof never performs.
		const std::filesystem::path activeRegistryPath = runtimePaths.m_ShaderArtifactRoot /
			"active" / GetTargetProfileName(backend) / "program-registry.ggsh.active";
		std::ifstream activeRegistryStream(activeRegistryPath, std::ios::binary);
		context.Check(activeRegistryStream.good(),
			"Injected artifact root opens the packaged active registry independently of process CWD");
#else
		GGLAB_UNUSED(backend);
		context.Check(runtimePaths.m_ShaderArtifactRoot == runtimePaths.m_IblDerivedDataRoot.parent_path().parent_path() / "ShaderArtifacts",
			"Artifact and derived-data paths share the explicit writable root independently of process CWD");
#endif
	}

	void RunApplicationArtifactPackageClosureSelfTests(SelfTestContext& context,
		const RuntimePaths& runtimePaths, RHIBackendType backend) noexcept
	{
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
