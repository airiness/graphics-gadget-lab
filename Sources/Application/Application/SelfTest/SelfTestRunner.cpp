#include "Application/SelfTest/SelfTestRunner.h"
#include "Application/SelfTest/DevToolsViewProfileSelfTests.h"
#include "Application/SelfTest/LaunchOptionsSelfTests.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTests.h"
#include "Application/SelfTest/VulkanQualificationSelfTests.h"
#if GGLAB_ENABLE_VULKAN
#include "Application/SelfTest/DevelopGuiVulkanPresentationContractSelfTests.h"
#endif
#include "GGLabFoundation/Logging/Log.h"
#include "GGLabTestCore/SelfTest.h"

#include <algorithm>
#include <array>

namespace gglab
{
	namespace
	{
		constexpr std::array RegisteredSuites{
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
		return selection == AllApplicationSelfTestsSelection || FindSuite(selection) != nullptr;
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
}
