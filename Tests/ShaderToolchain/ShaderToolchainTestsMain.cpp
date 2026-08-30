#include "GGLabFoundation/Logging/Log.h"
#include "GGLabTestCore/SelfTest.h"
#include "ShaderCompilerCliContractSelfTests.h"
#include "ShaderGraphPreviewProgramContractSelfTests.h"
#include "ShaderPreviewPublicationContractSelfTests.h"
#include "ShaderRuntimeArtifactPublicationSelfTests.h"

#include <algorithm>
#include <string_view>

namespace
{
	constexpr gglab::SelfTestSuiteDesc RegisteredSuites[]{
		gglab::SelfTestSuiteDesc{
			.m_Id = "shaderc-cli-contracts",
			.m_Run = &gglab::RunShaderCompilerCliContractSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "shader-graph-preview-program-contracts",
			.m_Run = &gglab::RunShaderGraphPreviewProgramContractSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "shader-preview-publication-contracts",
			.m_Run = &gglab::RunShaderPreviewPublicationContractSelfTests,
		},
		gglab::SelfTestSuiteDesc{
			.m_Id = "shader-artifact-publication",
			.m_Run = &gglab::RunShaderRuntimeArtifactPublicationSelfTests,
		},
	};

	[[nodiscard]] const gglab::SelfTestSuiteDesc* FindSuite(std::string_view suiteId) noexcept
	{
		const auto iterator =
			std::ranges::find(RegisteredSuites, suiteId, &gglab::SelfTestSuiteDesc::m_Id);
		return iterator != std::ranges::end(RegisteredSuites) ? &*iterator : nullptr;
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
