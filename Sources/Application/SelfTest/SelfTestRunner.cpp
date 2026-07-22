#include "Core/Precompiled.h"
#include "Application/SelfTest/SelfTestRunner.h"
#include "Application/SelfTest/ArtifactCacheSelfTests.h"
#include "Application/SelfTest/AssetDataSelfTests.h"
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
		};

		[[nodiscard]] const SelfTestSuiteDesc* FindSuite(
			std::string_view suiteId) noexcept
		{
			const auto iterator = std::ranges::find(
				RegisteredSuites,
				suiteId,
				&SelfTestSuiteDesc::m_Id);
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
