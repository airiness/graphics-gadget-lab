#include "Application/SelfTest/SelfTestRunner.h"
#include "Application/SelfTest/ApplicationHostConfigurationSelfTests.h"
#include "Application/SelfTest/ApplicationInputSelfTests.h"
#include "Application/SelfTest/ApplicationLifecycleSelfTests.h"
#include "Application/SelfTest/DevToolsViewProfileSelfTests.h"
#include "Application/SelfTest/LaunchOptionsSelfTests.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTests.h"
#include "Application/SelfTest/VulkanQualificationSelfTests.h"
#if GGLAB_ENABLE_VULKAN
#include "Application/SelfTest/DevelopGuiVulkanPresentationContractSelfTests.h"
#endif
#include "GGLabFoundation/Logging/Log.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/Asset/AssetPaths.h"
#include "RuntimePaths.h"

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
				.m_Id = "app-vulkan-qualification",
				.m_Run = &RunVulkanQualificationSelfTests,
			},
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
	}

	bool IsApplicationSelfTestSelectionValid(std::string_view selection) noexcept
	{
		return selection == AllApplicationSelfTestsSelection ||
			selection == ApplicationPathCompositionSelfTestSelection ||
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

	bool RunApplicationPathCompositionSelfTest(const RuntimePaths& runtimePaths) noexcept
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

		const std::filesystem::path shaderPath =
			runtimePaths.m_ShaderSourceRoot / "Passes" / "PassSkybox.hlsl";
		std::ifstream shaderStream(shaderPath, std::ios::binary);
		context.Check(shaderStream.good(),
			"Injected shader root opens packaged source independently of process CWD");

		reporter.OnSuiteFinished(
			ApplicationPathCompositionSelfTestSelection, context.GetSummary());
		return context.GetSummary().Succeeded();
	}
}
