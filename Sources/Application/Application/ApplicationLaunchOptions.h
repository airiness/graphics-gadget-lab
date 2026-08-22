#pragma once
#include "Graphics/RHI/RHITypes.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gglab
{
	enum class ApplicationStartupDemo : uint8_t
	{
		Start,
		Playground,
		LabHost,
	};

	struct ApplicationLaunchOptions
	{
		ApplicationStartupDemo m_StartupDemo = ApplicationStartupDemo::Start;
		std::optional<std::string> m_StartupLabId;
		std::optional<std::string> m_SelfTestSelection;
		bool m_StartWithAbsoluteMouse = false;
		bool m_DisableDevelopmentTools = false;

		// RHI backend selection. Defaults to DX12; an explicit --rhi vulkan
		// never falls back to DX12.
		RHIBackendType m_RhiBackend = RHIBackendType::DX12;
		bool m_RhiBackendSpecified = false;
		// Lists all Vulkan adapters with their profile evaluation and exits.
		bool m_ListAdapters = false;
		// Runs the standalone Vulkan hardware qualification and exits.
		bool m_RunVulkanQualification = false;
		// Optional deterministic adapter selector (enumeration index or
		// identity prefix) for the Vulkan backend.
		std::optional<std::string> m_AdapterSelector;
	};

	struct ApplicationLaunchParseResult
	{
		ApplicationLaunchOptions m_Options{};
		std::string m_Error;
		bool m_ShowHelp = false;

		[[nodiscard]] bool IsValid() const noexcept { return m_Error.empty(); }
	};

	ApplicationLaunchParseResult ParseApplicationLaunchOptions(
		std::span<const std::string_view> arguments) noexcept;
	std::string_view GetApplicationLaunchUsage() noexcept;
}
