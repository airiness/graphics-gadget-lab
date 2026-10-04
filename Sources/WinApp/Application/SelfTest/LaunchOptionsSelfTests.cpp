#include "Application/SelfTest/LaunchOptionsSelfTests.h"

#include "Application/ApplicationLaunchOptions.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	namespace
	{
		void RunPlaygroundContentCliContractTests(SelfTestContext& context) noexcept
		{
			for (const auto alias : { "playground", "Demo.Playground", "PLAYGROUND", "dEmO.pLaYgRoUnD" })
			{
				const std::vector<std::string_view> args = { "--demo", alias };
				const auto result = ParseApplicationLaunchOptions(args);
				context.Check(!result.IsValid() && result.m_Error.find("Unknown demo") != std::string::npos,
					"The retired original Playground aliases are rejected without selecting another Demo");
			}

			struct ContentAlias
			{
				std::string_view m_Alias;
				ApplicationStartupDemo m_Demo;
			};
			const ContentAlias aliases[] = {
				{ "island", ApplicationStartupDemo::Island },
				{ "Demo.Playground.Island", ApplicationStartupDemo::Island },
				{ "atrium", ApplicationStartupDemo::CoastalAtrium },
				{ "Demo.Playground.CoastalAtrium", ApplicationStartupDemo::CoastalAtrium },
			};
			for (const auto& alias : aliases)
			{
				const std::vector<std::string_view> args = { "--demo", alias.m_Alias };
				const auto result = ParseApplicationLaunchOptions(args);
				context.Check(result.IsValid() &&
					result.m_Options.m_StartupDemo == alias.m_Demo &&
					!result.m_Options.m_StartWithRelativeMouse,
					"Playground alias selects its content preset with default absolute mouse input");
				const std::vector<std::string_view> conflict = {
					"--demo", alias.m_Alias, "--lab", "gglab.lab.culling" };
				context.Check(!ParseApplicationLaunchOptions(conflict).IsValid(),
					"Playground content cannot be silently replaced by a Lab selection");
			}
		}

		void RunTextureContractLabCliTests(SelfTestContext& context) noexcept
		{
			for (const auto backend : { "dx12", "vulkan" })
			{
				const std::vector<std::string_view> args = {
					"--lab", "gglab.lab.texture_contract", "--rhi", backend, "--relative-mouse" };
				const auto result = ParseApplicationLaunchOptions(args);
				context.Check(result.IsValid() &&
					result.m_Options.m_StartupDemo == ApplicationStartupDemo::LabHost &&
					result.m_Options.m_StartupLabId == "gglab.lab.texture_contract" &&
					result.m_Options.m_StartWithRelativeMouse &&
					result.m_Options.m_RhiBackend == (std::string_view(backend) == "dx12" ?
						RHIBackendType::DX12 : RHIBackendType::Vulkan),
					"Texture contract starts through LabHost on the requested backend with relative mouse input");
			}
		}

		void RunLaunchOptionContractTests(SelfTestContext& context) noexcept
		{
			const auto parse = [](std::initializer_list<std::string_view> arguments)
				{
					const std::vector<std::string_view> args(arguments);
					return ParseApplicationLaunchOptions(args);
				};

			// --adapter requires an explicit --rhi vulkan.
			context.Check(parse({ "--state-root", "C:/gglab-state" }).IsValid() &&
				!parse({ "--state-root", "relative" }).IsValid() &&
				!parse({ "--state-root" }).IsValid() &&
				!parse({ "--state-root", "C:/one", "--state-root", "C:/two" }).IsValid(),
				"Explicit state root accepts one absolute path and rejects malformed or duplicate options");
			{
				const auto result = parse({ "--adapter", "0" });
				context.Check(!result.IsValid() && result.m_Error.find("--rhi vulkan") !=
					std::string::npos,
					"--adapter without --rhi vulkan is a parse error");
			}
			{
				const auto result = parse({ "--rhi", "dx12", "--adapter", "0" });
				context.Check(!result.IsValid() && result.m_Error.find("--rhi vulkan") !=
					std::string::npos,
					"--rhi dx12 with --adapter is a parse error");
			}
			{
				const auto result = parse({ "--rhi", "vulkan", "--adapter", "0" });
				context.Check(result.IsValid() &&
					result.m_Options.m_RhiBackend == RHIBackendType::Vulkan &&
					result.m_Options.m_AdapterSelector == "0",
					"--rhi vulkan with --adapter is valid");
			}
			// --list-adapters stands alone; combining it with --adapter fails.
			{
				const auto result = parse({ "--list-adapters" });
				context.Check(result.IsValid() && result.m_Options.m_ListAdapters,
					"--list-adapters is valid without --rhi vulkan");
			}
			{
				const auto result = parse({ "--list-adapters", "--adapter", "0" });
				context.Check(!result.IsValid() && result.m_Error.find("--list-adapters") !=
					std::string::npos,
					"--list-adapters with --adapter is a parse error");
			}
			{
				const auto result = parse({ "--rhi", "vulkan", "--list-adapters" });
				context.Check(result.IsValid() && result.m_Options.m_ListAdapters &&
					result.m_Options.m_RhiBackend == RHIBackendType::Vulkan,
					"--rhi vulkan with --list-adapters is valid");
			}
			// --list-adapters is Vulkan inspection; an explicit DX12 backend
			// conflicts with it.
			{
				const auto result = parse({ "--rhi", "dx12", "--list-adapters" });
				context.Check(!result.IsValid() && result.m_Error.find("--list-adapters") !=
					std::string::npos,
					"--rhi dx12 with --list-adapters is a parse error");
			}

			{
				const auto result = parse({ "--self-test", "app-path-composition" });
				context.Check(result.IsValid() && result.m_Options.m_SelfTestSelection ==
					"app-path-composition",
					"Path-composition proof is an explicit self-test exit mode");
			}
			{
				const auto unknownSuite = parse({ "--self-test", "unknown-suite" });
				const auto interactiveConflict = parse({ "--self-test", "all", "--lab", "gglab.lab.culling" });
				const auto relativeMouseConflict = parse({ "--self-test", "all", "--relative-mouse" });
				context.Check(!unknownSuite.IsValid() &&
					unknownSuite.m_Error.find("Unknown self-test selection") != std::string::npos,
					"Self-test selection rejects unknown suites");
				context.Check(!interactiveConflict.IsValid() &&
					interactiveConflict.m_Error.find("cannot be combined") != std::string::npos &&
					!relativeMouseConflict.IsValid() &&
					relativeMouseConflict.m_Error.find("cannot be combined") != std::string::npos,
					"Self-test selection rejects interactive startup options");
			}
			{
				const auto result = parse({ "--no-devtools" });
				context.Check(result.IsValid() &&
					result.m_Options.m_DisableDevelopmentTools,
					"--no-devtools disables optional desktop tooling for production startup");
			}
			{
				const auto result = parse({ "--no-devtools", "--no-devtools" });
				context.Check(!result.IsValid(),
					"--no-devtools rejects duplicate specification");
			}
			{
				const auto unknownOption = parse({ "--unknown-option" });
				const auto removedMouseOption = parse({ "--absolute-mouse" });
				const auto unknownDemo = parse({ "--demo", "unknown" });
				context.Check(!unknownOption.IsValid() &&
					unknownOption.m_Error.find("Unknown option") != std::string::npos &&
					!removedMouseOption.IsValid() &&
					removedMouseOption.m_Error.find("Unknown option") != std::string::npos,
					"Launch options reject unknown flags, including the removed absolute mouse flag");
				context.Check(!unknownDemo.IsValid() &&
					unknownDemo.m_Error.find("Unknown demo") != std::string::npos,
					"Launch options reject unknown demo names");
			}
		}

	}

	void RunLaunchOptionsSelfTests(SelfTestContext& context) noexcept
	{
		RunLaunchOptionContractTests(context);
		RunPlaygroundContentCliContractTests(context);
		RunTextureContractLabCliTests(context);
	}
}
