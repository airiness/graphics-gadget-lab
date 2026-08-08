#include "Core/Precompiled.h"
#include "Application/RenderingStartup.h"
#include "Core/Log/Logger.h"
#if GGLAB_ENABLE_VULKAN
#include "Graphics/RHI/Vulkan/VulkanBootstrap.h"
#endif

#include <charconv>

namespace gglab
{
	namespace
	{
#if GGLAB_ENABLE_VULKAN
		[[nodiscard]] VulkanAdapterSelectionRequest MakeVulkanSelectionRequest(
			const std::optional<std::string>& selector) noexcept
		{
			VulkanAdapterSelectionRequest request{};
			if (!selector)
			{
				return request;
			}

			const std::string& value = *selector;
			uint64_t parsedIndex = 0;
			const bool isIndex = !value.empty() &&
				std::ranges::all_of(value,
					[](char c) noexcept { return c >= '0' && c <= '9'; }) &&
				value.size() <= 10 &&
				(std::from_chars(value.data(), value.data() + value.size(), parsedIndex).ec ==
					std::errc{});
			if (isIndex)
			{
				request.m_Kind = VulkanAdapterSelectionKind::Index;
				request.m_Index = static_cast<uint32_t>(parsedIndex);
				return request;
			}
			request.m_Kind = VulkanAdapterSelectionKind::Prefix;
			request.m_Prefix = value;
			return request;
		}
#endif
	}

	int RunRenderingStartupPath(
		const ApplicationLaunchOptions& options, HWND hwnd) noexcept
	{
#if GGLAB_ENABLE_VULKAN
		VulkanBootstrapOptions bootstrapOptions{};
		bootstrapOptions.m_HInstance = GetModuleHandle(nullptr);
		bootstrapOptions.m_Hwnd = hwnd;
		bootstrapOptions.m_RequestValidation = true;
		bootstrapOptions.m_IsDebugBuild = true;
		bootstrapOptions.m_SelectionRequest =
			MakeVulkanSelectionRequest(options.m_AdapterSelector);
		if (options.m_ListAdapters)
		{
			bootstrapOptions.m_SelectionRequest = {};
		}

		VulkanBootstrapReport report;
		return RunVulkanBootstrap(bootstrapOptions, report);
#else
		GGLAB_UNUSED(options);
		GGLAB_UNUSED(hwnd);
		if (auto& logger = Logger::GetLogger(Logger::LoggerType::Application))
		{
			logger->error("Vulkan backend was not built (GGLAB_ENABLE_VULKAN=0).");
		}
		return 1;
#endif
	}
}
