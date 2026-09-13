#include "GGLabFoundation/Logging/Log.h"
#include "GGLabTestCore/SelfTest.h"
#include "ShaderCompileContractSelfTests.h"

#include <string_view>

namespace
{
	constexpr std::string_view ShaderCompileContractsSuiteId = "shader-compile-contracts";

	[[nodiscard]] bool RunSelection(std::string_view selection) noexcept
	{
		if (selection != "all" && selection != ShaderCompileContractsSuiteId)
		{
			return false;
		}

		gglab::InitializeLogging();
		gglab::ConsoleSelfTestReporter reporter;
		return gglab::RunSelfTestSuite({
			.m_Id = ShaderCompileContractsSuiteId,
			.m_Run = &gglab::RunShaderCompileContractSelfTests,
			}, reporter);
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
