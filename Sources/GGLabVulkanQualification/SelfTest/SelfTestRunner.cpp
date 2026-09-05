#include "SelfTest/SelfTestRunner.h"

#include "GGLabTestCore/SelfTest.h"
#include "QualificationLaunchOptions.h"
#include "SelfTest/VulkanQualificationSelfTests.h"

#include <array>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	namespace
	{
		void RunQualificationLaunchOptionsSelfTests(SelfTestContext& context) noexcept
		{
			const auto parse = [](std::initializer_list<std::string_view> arguments)
				{
					const std::vector<std::string_view> args(arguments);
					return ParseVulkanQualificationLaunchOptions(args);
				};

			context.Check(parse({}).IsValid(),
				"qualification runs with the default adapter when no option is supplied");
			const auto selectedAdapter = parse({ "--adapter", "0" });
			context.Check(selectedAdapter.IsValid() &&
				selectedAdapter.m_Options.m_AdapterSelector == "0",
				"qualification accepts a deterministic adapter selector");
			context.Check(!parse({ "--adapter" }).IsValid(),
				"qualification rejects a missing adapter selector value");
			context.Check(!parse({ "--adapter", "0", "--adapter", "1" }).IsValid(),
				"qualification rejects duplicate adapter selectors");
			const auto selfTest = parse({ "--self-test" });
			context.Check(selfTest.IsValid() && selfTest.m_Options.m_RunSelfTests,
				"qualification exposes an explicit headless self-test mode");
			context.Check(!parse({ "--self-test", "--adapter", "0" }).IsValid(),
				"headless self-tests reject hardware adapter selection");
			context.Check(parse({ "--help" }).m_ShowHelp,
				"qualification exposes command-line help");
			context.Check(!parse({ "--vulkan-qualification" }).IsValid(),
				"qualification executable rejects the removed WinApp dispatch option");
			context.Check(!parse({ "--unknown" }).IsValid(),
				"qualification rejects unknown options");
		}
	}

	bool RunVulkanQualificationSelfTestSuites() noexcept
	{
		constexpr std::array suites{
			SelfTestSuiteDesc{
				.m_Id = "vulkan-qualification-launch-options",
				.m_Run = &RunQualificationLaunchOptionsSelfTests,
			},
			SelfTestSuiteDesc{
				.m_Id = "vulkan-qualification-contracts",
				.m_Run = &RunVulkanQualificationSelfTests,
			},
		};

		ConsoleSelfTestReporter reporter;
		bool allSucceeded = true;
		for (const SelfTestSuiteDesc& suite : suites)
		{
			if (!RunSelfTestSuite(suite, reporter))
			{
				allSucceeded = false;
			}
		}
		return allSucceeded;
	}
}
