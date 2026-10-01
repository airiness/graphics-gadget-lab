#include "GGLabFoundation/Logging/Log.h"
#include "GGLabTestCore/SelfTest.h"
#include "ShaderCompileContractSelfTests.h"
#include "BrdfLutNumericSelfTests.h"

#include <cstdio>
#include <string_view>

namespace
{
	constexpr std::string_view ShaderCompileContractsSuiteId = "shader-compile-contracts";

	class GpuSelfTestReporter final : public gglab::SelfTestReporterBase
	{
	public:
		void OnSuiteStarted(std::string_view suite) noexcept override
		{
			std::printf("Running GPU %.*s self-tests (hidden window)...\n", static_cast<int>(suite.size()), suite.data());
		}
		void OnCheckCompleted(std::string_view name, bool succeeded) noexcept override
		{
			m_Console.OnCheckCompleted(name, succeeded);
		}
		void OnSuiteFinished(std::string_view suite, const gglab::SelfTestSummary& summary) noexcept override
		{
			m_Console.OnSuiteFinished(suite, summary);
		}
	private:
		gglab::ConsoleSelfTestReporter m_Console;
	};

	[[nodiscard]] bool RunSelection(std::string_view selection, std::string_view backend) noexcept
	{
		gglab::InitializeLogging();
		gglab::ConsoleSelfTestReporter reporter;
		if (selection == "brdf-lut-gpu")
		{
			GpuSelfTestReporter gpuReporter;
			return gglab::RunSelfTestSuite({ .m_Id = selection,
				.m_Run = backend == "dx12" ? &gglab::RunDX12BrdfLutNumericSelfTests : &gglab::RunVulkanBrdfLutNumericSelfTests }, gpuReporter);
		}
		bool passed = true;
		if (selection == "all" || selection == ShaderCompileContractsSuiteId)
		{
			passed &= gglab::RunSelfTestSuite({ .m_Id = ShaderCompileContractsSuiteId,
				.m_Run = &gglab::RunShaderCompileContractSelfTests }, reporter);
		}
		if (selection == "all" || selection == "brdf-lut-reference")
		{
			passed &= gglab::RunSelfTestSuite({ .m_Id = "brdf-lut-reference",
				.m_Run = &gglab::RunBrdfLutReferenceSelfTests }, reporter);
		}
		return passed;
	}
}

int main(int argumentCount, char* arguments[])
{
	std::string_view selection = "all";
	std::string_view backend;
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
		else if (argument == "--rhi" && index + 1 < argumentCount)
		{
			backend = arguments[++index];
		}
		else
		{
			return 2;
		}
	}
	if ((selection == "brdf-lut-gpu" && backend != "dx12" && backend != "vulkan") ||
		(selection != "brdf-lut-gpu" && !backend.empty()) ||
		(selection != "all" && selection != ShaderCompileContractsSuiteId &&
			selection != "brdf-lut-reference" && selection != "brdf-lut-gpu"))
	{
		std::fputs("Usage: GGLabShaderRuntimeIntegrationTests.exe --suite <all|shader-compile-contracts|brdf-lut-reference>\n"
			"       GGLabShaderRuntimeIntegrationTests.exe --suite brdf-lut-gpu --rhi <dx12|vulkan>\n", stderr);
		return 2;
	}
	return RunSelection(selection, backend) ? 0 : 1;
}
