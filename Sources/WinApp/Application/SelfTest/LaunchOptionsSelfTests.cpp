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
				const std::vector<std::string_view> args = { "--demo", alias.m_Alias, "--absolute-mouse" };
				const auto result = ParseApplicationLaunchOptions(args);
				context.Check(result.IsValid() &&
					result.m_Options.m_StartupDemo == alias.m_Demo &&
					result.m_Options.m_StartWithAbsoluteMouse,
					"Playground alias selects its content preset with absolute mouse input");
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
					"--lab", "gglab.lab.texture_contract", "--rhi", backend, "--absolute-mouse" };
				const auto result = ParseApplicationLaunchOptions(args);
				context.Check(result.IsValid() &&
					result.m_Options.m_StartupDemo == ApplicationStartupDemo::LabHost &&
					result.m_Options.m_StartupLabId == "gglab.lab.texture_contract" &&
					result.m_Options.m_StartWithAbsoluteMouse &&
					result.m_Options.m_RhiBackend == (std::string_view(backend) == "dx12" ?
						RHIBackendType::DX12 : RHIBackendType::Vulkan),
					"Texture contract starts through LabHost on the requested backend");
			}
			for (const auto alias : { "texture-contract", "Demo.Playground.TextureContract" })
			{
				const std::vector<std::string_view> args = { "--demo", alias };
				context.Check(!ParseApplicationLaunchOptions(args).IsValid(),
					"Texture contract has no duplicate standalone Demo entry");
			}
		}

		void RunVulkanCliContractTests(SelfTestContext& context) noexcept
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

			// Standalone qualification has its own executable and is not a WinApp mode.
			{
				const auto result = parse({ "--vulkan-qualification" });
				context.Check(!result.IsValid(),
					"WinApp rejects the standalone qualification executable's former option");
			}
			{
				const auto result = parse({ "--self-test", "app-path-composition" });
				context.Check(result.IsValid() && result.m_Options.m_SelfTestSelection ==
					"app-path-composition",
					"Path-composition proof is an explicit self-test exit mode");
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
		}

		void RunShaderPreviewSessionCliContractTests(SelfTestContext& context) noexcept
		{
			const auto parse = [](std::initializer_list<std::string_view> arguments)
				{
					const std::vector<std::string_view> args(arguments);
					return ParseApplicationLaunchOptions(args);
				};
			constexpr std::string_view SessionId = "0123456789abcdef0123456789abcdef";

			{
				const auto result = parse({ "--lab", "gglab.lab.shader_graph_preview",
					"--shader-preview-session", SessionId });
				context.Check(result.IsValid() &&
					result.m_Options.m_StartupDemo == ApplicationStartupDemo::LabHost &&
					result.m_Options.m_ShaderPreviewSessionId == SessionId,
					"Shader Preview session attachment requires the stable Preview Lab ID");
			}
			{
				const auto result = parse({ "--shader-preview-session", SessionId });
				context.Check(!result.IsValid() && result.m_Error.find("--lab") != std::string::npos,
					"Shader Preview session attachment cannot silently select a Lab");
			}
			{
				const auto result = parse({ "--lab", "gglab.lab.culling",
					"--shader-preview-session", SessionId });
				context.Check(!result.IsValid(),
					"Shader Preview session attachment rejects every non-Preview Lab");
			}
			{
				const auto result = parse({ "--lab", "gglab.lab.shader_graph_preview",
					"--shader-preview-session", "0123456789ABCDEF0123456789ABCDEF" });
				context.Check(!result.IsValid(),
					"Shader Preview session IDs reject uppercase hexadecimal characters");
			}
			{
				const auto result = parse({ "--lab", "gglab.lab.shader_graph_preview",
					"--shader-preview-session", "0123456789abcdef" });
				context.Check(!result.IsValid(),
					"Shader Preview session IDs reject the wrong length before path construction");
			}
			{
				const auto result = parse({ "--lab", "gglab.lab.shader_graph_preview",
					"--shader-preview-session", SessionId,
					"--shader-preview-session", SessionId });
				context.Check(!result.IsValid(),
					"Shader Preview session attachment rejects duplicate session options");
			}
			{
				const auto result = parse({ "--lab", "gglab.lab.shader_graph_preview",
					"--shader-preview-session", SessionId, "--list-adapters" });
				context.Check(!result.IsValid(),
					"Shader Preview session attachment cannot be discarded by an adapter-list exit mode");
			}
		}
	}

	void RunLaunchOptionsSelfTests(SelfTestContext& context) noexcept
	{
		RunVulkanCliContractTests(context);
		RunPlaygroundContentCliContractTests(context);
		RunTextureContractLabCliTests(context);
		RunShaderPreviewSessionCliContractTests(context);
	}
}
