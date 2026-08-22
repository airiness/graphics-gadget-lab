#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace gglab
{
	enum class AppRuntimeRHIBackend : uint8_t
	{
		Unknown,
		DX12,
		Vulkan,
	};

	enum class AppRuntimeStartupDemo : uint8_t
	{
		Start,
		Playground,
		LabHost,
	};

	enum class AppRuntimePointerMode : uint8_t
	{
		Relative,
		Absolute,
	};

	enum class AppRuntimeCapability : uint32_t
	{
		None = 0,
		BuiltInContent = 1u << 0,
		DevelopmentTools = 1u << 1,
	};

	[[nodiscard]] constexpr AppRuntimeCapability operator|(
		AppRuntimeCapability lhs, AppRuntimeCapability rhs) noexcept
	{
		return static_cast<AppRuntimeCapability>(
			static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
	}

	struct AppRuntimeExtent
	{
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
	};

	struct AppRuntimeConfig
	{
		AppRuntimeRHIBackend m_RhiBackend = AppRuntimeRHIBackend::Unknown;
		std::optional<std::string> m_AdapterSelector;
		AppRuntimeStartupDemo m_StartupDemo = AppRuntimeStartupDemo::Start;
		std::optional<std::string> m_StartupLabId;
		AppRuntimeExtent m_InitialExtent{};
		AppRuntimePointerMode m_InitialPointerMode = AppRuntimePointerMode::Relative;
		AppRuntimeCapability m_Capabilities = AppRuntimeCapability::None;
		bool m_RequestRuntimeValidation = false;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] bool HasCapability(AppRuntimeCapability capability) const noexcept;
	};
}
