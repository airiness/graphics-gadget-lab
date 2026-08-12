#include "Application/SelfTest/SelfTestRunner.h"
#include "Application/SelfTest/ArtifactCacheSelfTests.h"
#include "Application/SelfTest/AssetDataSelfTests.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTests.h"
#include "Application/SelfTest/PublicationAccountingSelfTests.h"
#include "Application/SelfTest/RenderingContractSelfTests.h"
#include "Application/SelfTest/SelfTest.h"
#include "Application/SelfTest/VulkanContractSelfTests.h"
#include "Core/Log/Logger.h"

namespace gglab
{
	namespace
	{
		constexpr std::array RegisteredSuites{
			SelfTestSuiteDesc{
				.m_Id = "artifact-cache",
				.m_Run = &RunArtifactCacheSelfTests,
			},
			SelfTestSuiteDesc{
				.m_Id = "asset-data",
				.m_Run = &RunAssetDataSelfTests,
			},
			SelfTestSuiteDesc{
				.m_Id = "napa-voxel",
				.m_Run = &RunNapaVoxelCoreSelfTests,
			},
			SelfTestSuiteDesc{
				.m_Id = "publication-accounting",
				.m_Run = &RunPublicationAccountingSelfTests,
			},
			SelfTestSuiteDesc{
				.m_Id = "rendering-contracts",
				.m_Run = &RunRenderingContractSelfTests,
			},
			SelfTestSuiteDesc{
				.m_Id = "vulkan-contracts",
				.m_Run = &RunVulkanContractSelfTests,
			},
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

		if (!Logger::GetLogger(Logger::LoggerType::Application))
		{
			Logger::Initialize();
		}
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
