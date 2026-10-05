#pragma once
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gglab
{
	enum class ApplicationStartupDemo : uint8_t
	{
		Start,
		Island,
		CoastalAtrium,
		LabHost,
	};

	// One after-ready capture taken by an unattended launch, which then exits
	// with the capture result.
	struct ApplicationCaptureOnReadyOptions
	{
		std::filesystem::path m_OutputDirectory;
		FrameCaptureSource m_Source = FrameCaptureSource::Scene;
		uint32_t m_SettleFrames = 8;
		std::string m_Label;
		// Camera reference view of the startup content; empty keeps its camera.
		std::string m_ReferenceViewId;
		// Wall-clock limit from the first rendered frame until the capture finishes.
		double m_TimeoutSeconds = 120.0;
	};

	struct ApplicationLaunchOptions
	{
		ApplicationStartupDemo m_StartupDemo = ApplicationStartupDemo::Start;
		std::optional<std::string> m_StartupLabId;
		std::optional<std::string> m_SelfTestSelection;
		std::filesystem::path m_StateRoot;
		bool m_StartWithRelativeMouse = false;
		bool m_DisableDevelopmentTools = false;

		// RHI backend selection. Defaults to DX12; an explicit --rhi vulkan
		// never falls back to DX12.
		RHIBackendType m_RhiBackend = RHIBackendType::DX12;
		bool m_RhiBackendSpecified = false;
		// Lists all Vulkan adapters with their profile evaluation and exits.
		bool m_ListAdapters = false;
		// Optional deterministic adapter selector (enumeration index or
		// identity prefix) for the Vulkan backend.
		std::optional<std::string> m_AdapterSelector;

		// Client size of the main window and therefore of the display target.
		uint32_t m_WindowWidth = 1920;
		uint32_t m_WindowHeight = 1080;
		bool m_WindowSizeSpecified = false;
		// The main window is never shown or activated, and input is ignored.
		bool m_Hidden = false;
		std::optional<double> m_FixedDeltaTimeSeconds;
		std::optional<ApplicationCaptureOnReadyOptions> m_CaptureOnReady;
		// Serves the session control protocol on \\.\pipe\gglab-session-<id>.
		std::optional<std::string> m_SessionId;
		// A session exits after this long without a control request.
		double m_IdleTimeoutSeconds = 900.0;
		bool m_IdleTimeoutSpecified = false;
		// stdout and stderr are written to this file instead of the inherited handles.
		std::filesystem::path m_OutputLog;
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
