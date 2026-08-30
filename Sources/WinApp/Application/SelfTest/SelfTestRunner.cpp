#include "Application/SelfTest/SelfTestRunner.h"
#include "Application/SelfTest/ApplicationHostConfigurationSelfTests.h"
#include "Application/SelfTest/ApplicationContentRegistrationSelfTests.h"
#include "Application/SelfTest/ApplicationInputSelfTests.h"
#include "Application/SelfTest/ApplicationLifecycleSelfTests.h"
#include "Application/SelfTest/DevToolsViewProfileSelfTests.h"
#include "Application/SelfTest/LaunchOptionsSelfTests.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTests.h"
#include "Application/SelfTest/ShaderPreviewRuntimeSessionSelfTests.h"
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
#include "Application/SelfTest/DevelopmentShaderBuildProcessClientSelfTests.h"
#endif
#if GGLAB_ENABLE_VULKAN
#include "Application/SelfTest/DevelopGuiVulkanPresentationContractSelfTests.h"
#endif
#include "GGLabFoundation/Logging/Log.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/Asset/AssetPaths.h"
#include "RuntimePaths.h"
#include "ShaderArtifactRuntime/ShaderArtifactStore.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"
#include "ShaderArtifactRuntime/VulkanShaderRuntimeABI.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

namespace gglab
{
	namespace
	{
		constexpr std::array RegisteredSuites{
			SelfTestSuiteDesc{
				.m_Id = "app-content-registration",
				.m_Run = &RunApplicationContentRegistrationSelfTests,
			},
			SelfTestSuiteDesc{
				.m_Id = "app-input",
				.m_Run = &RunApplicationInputSelfTests,
			},
			SelfTestSuiteDesc{
				.m_Id = "app-host-configuration",
				.m_Run = &RunApplicationHostConfigurationSelfTests,
			},
			SelfTestSuiteDesc{
				.m_Id = "app-lifecycle",
				.m_Run = &RunApplicationLifecycleSelfTests,
			},
			SelfTestSuiteDesc{
				.m_Id = "app-devtools-view-profile",
				.m_Run = &RunDevToolsViewProfileSelfTests,
			},
			SelfTestSuiteDesc{
				.m_Id = "app-launch-options",
				.m_Run = &RunLaunchOptionsSelfTests,
			},
			SelfTestSuiteDesc{
				.m_Id = "app-shader-preview-session",
				.m_Run = &RunShaderPreviewRuntimeSessionSelfTests,
			},
#if !defined(GGLAB_ARTIFACT_ONLY_RUNTIME)
			SelfTestSuiteDesc{
				.m_Id = "app-development-shader-process-client",
				.m_Run = &RunDevelopmentShaderBuildProcessClientSelfTests,
			},
#endif
			SelfTestSuiteDesc{
				.m_Id = "napa-voxel",
				.m_Run = &RunNapaVoxelCoreSelfTests,
			},
#if GGLAB_ENABLE_VULKAN
			SelfTestSuiteDesc{
				.m_Id = "app-developgui-vulkan-presentation-contract",
				.m_Run = &RunDevelopGuiVulkanPresentationContractSelfTests,
			},
#endif
		};

		[[nodiscard]] const SelfTestSuiteDesc* FindSuite(std::string_view suiteId) noexcept
		{
			const auto iterator =
				std::ranges::find(RegisteredSuites, suiteId, &SelfTestSuiteDesc::m_Id);
			return iterator != RegisteredSuites.end() ? &*iterator : nullptr;
		}

		[[nodiscard]] constexpr ShaderTargetProfile GetTargetProfile(
			RHIBackendType backend) noexcept
		{
			return backend == RHIBackendType::Vulkan
				? ShaderTargetProfile::GGLabVulkan13
				: ShaderTargetProfile::GGLabDX12;
		}

		[[nodiscard]] constexpr std::string_view GetTargetProfileName(
			RHIBackendType backend) noexcept
		{
			return backend == RHIBackendType::Vulkan ? "gglab-vulkan13" : "gglab-dx12";
		}

		[[nodiscard]] constexpr ShaderArtifactCompatibilityRequest MakeCompatibilityRequest(
			RHIBackendType backend, ShaderStage stage) noexcept
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

	bool IsApplicationSelfTestSelectionValid(std::string_view selection) noexcept
	{
		return selection == AllApplicationSelfTestsSelection ||
			selection == ApplicationPathCompositionSelfTestSelection ||
			selection == ApplicationArtifactPackageClosureSelfTestSelection ||
			FindSuite(selection) != nullptr;
	}

	bool RunApplicationSelfTests(std::string_view selection) noexcept
	{
		const bool runAll = selection == AllApplicationSelfTestsSelection;
		const SelfTestSuiteDesc* suite = runAll ? nullptr : FindSuite(selection);
		if (!runAll && !suite)
		{
			return false;
		}

		InitializeLogging();
		ConsoleSelfTestReporter reporter;
		if (runAll)
		{
			bool allSucceeded = true;
			for (const SelfTestSuiteDesc& registeredSuite : RegisteredSuites)
			{
				if (!RunSelfTestSuite(registeredSuite, reporter))
				{
					allSucceeded = false;
				}
			}
			return allSucceeded;
		}
		return RunSelfTestSuite(*suite, reporter);
	}

	bool RunApplicationPathCompositionSelfTest(
		const RuntimePaths& runtimePaths, RHIBackendType backend) noexcept
	{
		InitializeLogging();
		ConsoleSelfTestReporter reporter;
		reporter.OnSuiteStarted(ApplicationPathCompositionSelfTestSelection);
		SelfTestContext context(reporter);

		std::error_code error;
		const std::filesystem::path currentDirectory = std::filesystem::current_path(error);
		context.Check(!error && currentDirectory.lexically_normal() !=
			runtimePaths.m_RuntimeRoot.lexically_normal(),
			"Path composition proof runs outside the executable runtime root");
		context.Check(runtimePaths.IsValid(),
			"Executable discovery produces valid absolute runtime paths");

		const std::filesystem::path assetPath =
			ResolveAssetPath(runtimePaths.m_AssetRoot, "Textures/UVTest1K.png");
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
		static_cast<void>(backend);
		context.Check(runtimePaths.m_ShaderArtifactRoot ==
			runtimePaths.m_RuntimeRoot / "ShaderArtifacts",
			"Executable discovery composes the development artifact root independently of process CWD");
#endif

		reporter.OnSuiteFinished(
			ApplicationPathCompositionSelfTestSelection, context.GetSummary());
		return context.GetSummary().Succeeded();
	}

	bool RunApplicationArtifactPackageClosureSelfTest(
		const RuntimePaths& runtimePaths, RHIBackendType backend) noexcept
	{
		InitializeLogging();
		ConsoleSelfTestReporter reporter;
		reporter.OnSuiteStarted(ApplicationArtifactPackageClosureSelfTestSelection);
		SelfTestContext context(reporter);

		const ShaderTargetProfile targetProfile = GetTargetProfile(backend);
		ShaderLooseActiveProgramRegistryReader activeReader(
			ShaderLooseActiveProgramRegistryLocator(
				runtimePaths.m_ShaderArtifactRoot, targetProfile));
		const ActiveShaderProgramRegistryReadResult active = activeReader.Read();
		context.Check(active.IsSuccess(), "The target-scoped active registry pointer is valid");

		ShaderProgramRegistryArtifactReadResult registry{};
		if (active.IsSuccess())
		{
			ShaderLooseProgramRegistryArtifactReader registryReader(
				ShaderLooseProgramRegistryArtifactLocator(runtimePaths.m_ShaderArtifactRoot));
			registry = registryReader.ReadArtifact(active.m_RegistryRef);
		}
		context.Check(registry.IsSuccess(),
			"The active pointer resolves to a valid immutable program registry");

		bool closureValid = registry.IsSuccess() && !registry.m_Artifact.m_Entries.empty();
		if (closureValid)
		{
			ShaderLooseArtifactReader artifactReader(
				ShaderLooseArtifactLocator(runtimePaths.m_ShaderArtifactRoot));
			ShaderArtifactStore artifactStore(artifactReader);
			for (const ShaderProgramRegistryEntry& entry : registry.m_Artifact.m_Entries)
			{
				if (entry.m_TargetProfile != targetProfile ||
					!artifactStore.LoadArtifact(entry.m_ArtifactRef,
						MakeCompatibilityRequest(backend, entry.m_ProgramRef.m_Stage)).IsSuccess())
				{
					closureValid = false;
					break;
				}
			}
		}
		context.Check(closureValid,
			"Every registry entry closes over a present, valid, compatible artifact payload");

		reporter.OnSuiteFinished(
			ApplicationArtifactPackageClosureSelfTestSelection, context.GetSummary());
		return context.GetSummary().Succeeded();
	}
}
