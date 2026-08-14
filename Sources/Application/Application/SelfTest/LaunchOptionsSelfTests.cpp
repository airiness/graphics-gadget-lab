#include "Application/SelfTest/LaunchOptionsSelfTests.h"

#include "Application/ApplicationLaunchOptions.h"
#include "Graphics/RHI/RHITypes.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	namespace
	{
		void RunVulkanCliContractTests(SelfTestContext& context) noexcept
		{
			const auto parse = [](std::initializer_list<std::string_view> arguments)
				{
					const std::vector<std::string_view> args(arguments);
					return ParseApplicationLaunchOptions(args);
				};

			// --adapter requires an explicit --rhi vulkan.
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
		}
	}

	void RunLaunchOptionsSelfTests(SelfTestContext& context) noexcept
	{
		RunVulkanCliContractTests(context);
	}
}
