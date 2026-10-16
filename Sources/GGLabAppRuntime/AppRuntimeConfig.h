#pragma once
#include "Lab/LabTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gglab
{
	enum class AppRuntimeRHIBackend : uint8_t
	{
		Unknown,
		DX12,
		Vulkan,
	};

	enum class AppRuntimePointerMode : uint8_t
	{
		Relative,
		Absolute,
	};

	enum class AppRuntimeCapability : uint32_t
	{
		None = 0,
		DevelopmentTools = 1u << 0,
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
		std::string m_StartupDemoId;
		std::optional<std::string> m_StartupLabId;
		// Parsed against the startup Lab when its session is created; requires a
		// startup Demo that provides the Lab runtime.
		std::vector<LabParameterAssignment> m_StartupLabParameters;
		AppRuntimeExtent m_InitialExtent{};
		AppRuntimePointerMode m_InitialPointerMode = AppRuntimePointerMode::Absolute;
		AppRuntimeCapability m_Capabilities = AppRuntimeCapability::None;
		bool m_RequestRuntimeValidation = false;
		bool m_RequestGpuBasedValidation = false;
		bool m_NonFiniteCounters = false;
		// Advances simulation time by exactly this many seconds per frame for every
		// Demo and Lab, so frame-count-based captures are reproducible.
		std::optional<double> m_FixedDeltaTimeSeconds;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] bool HasCapability(AppRuntimeCapability capability) const noexcept;
	};
}
