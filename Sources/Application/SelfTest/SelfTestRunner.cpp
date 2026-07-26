#include "Core/Precompiled.h"
#include "Application/SelfTest/SelfTestRunner.h"
#include "Application/SelfTest/ArtifactCacheSelfTests.h"
#include "Application/SelfTest/AssetDataSelfTests.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTests.h"
#include "Application/SelfTest/PublicationAccountingSelfTests.h"
#include "Application/SelfTest/RenderingContractSelfTests.h"
#include "Application/SelfTest/SelfTest.h"

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
		};

		[[nodiscard]] const SelfTestSuiteDesc* FindSuite(std::string_view suiteId) noexcept
		{
			const auto iterator =
				std::ranges::find(RegisteredSuites, suiteId, &SelfTestSuiteDesc::m_Id);
			return iterator != RegisteredSuites.end() ? &*iterator : nullptr;
		}
	}

	bool IsApplicationSelfTestSuiteRegistered(std::string_view suiteId) noexcept
	{
		return FindSuite(suiteId) != nullptr;
	}

	bool RunApplicationSelfTestSuite(std::string_view suiteId) noexcept
	{
		const SelfTestSuiteDesc* suite = FindSuite(suiteId);
		if (!suite)
		{
			return false;
		}

		ConsoleSelfTestReporter reporter;
		return RunSelfTestSuite(*suite, reporter);
	}
}
