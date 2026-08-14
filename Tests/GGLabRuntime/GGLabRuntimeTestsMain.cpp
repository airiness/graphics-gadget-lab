#include "ArtifactCacheSelfTests.h"
#include "AssetDataSelfTests.h"
#include "AssetUploadSchedulerSelfTests.h"
#include "GGLabFoundation/Logging/Log.h"
#include "GGLabTestCore/SelfTest.h"
#include "PublicationAccountingSelfTests.h"
#include "RenderingContractSelfTests.h"
#include "VulkanContractSelfTests.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace
{
	constexpr std::array RegisteredSuites{
		gglab::SelfTestSuiteDesc{
			.m_Id = "artifact-cache",
			.m_Run = &gglab::RunArtifactCacheSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "asset-data",
			.m_Run = &gglab::RunAssetDataSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "asset-upload-scheduler",
			.m_Run = &gglab::RunAssetUploadSchedulerSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "publication-accounting",
			.m_Run = &gglab::RunPublicationAccountingSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "rendering-contracts",
			.m_Run = &gglab::RunRenderingContractSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "vulkan-contracts",
			.m_Run = &gglab::RunVulkanContractSelfTests,
		},
	};

	[[nodiscard]] const gglab::SelfTestSuiteDesc* FindSuite(std::string_view suiteId) noexcept
	{
		const auto iterator =
			std::ranges::find(RegisteredSuites, suiteId, &gglab::SelfTestSuiteDesc::m_Id);
		return iterator != RegisteredSuites.end() ? &*iterator : nullptr;
	}

	[[nodiscard]] bool RunSelection(std::string_view selection) noexcept
	{
		const bool runAll = selection == "all";
		const gglab::SelfTestSuiteDesc* suite = runAll ? nullptr : FindSuite(selection);
		if (!runAll && !suite)
		{
			return false;
		}

		gglab::InitializeLogging();
		gglab::ConsoleSelfTestReporter reporter;
		if (runAll)
		{
			bool allSucceeded = true;
			for (const gglab::SelfTestSuiteDesc& registeredSuite : RegisteredSuites)
			{
				if (!gglab::RunSelfTestSuite(registeredSuite, reporter))
				{
					allSucceeded = false;
				}
			}
			return allSucceeded;
		}
		return gglab::RunSelfTestSuite(*suite, reporter);
	}
}

int main(int argumentCount, char* arguments[])
{
	std::string_view selection = "all";
	for (int index = 1; index < argumentCount; ++index)
	{
		const std::string_view argument = arguments[index];
		if (argument == "--suite")
		{
			if (index + 1 >= argumentCount)
			{
				return 2;
			}
			selection = arguments[++index];
		}
		else
		{
			return 2;
		}
	}
	return RunSelection(selection) ? 0 : 1;
}
