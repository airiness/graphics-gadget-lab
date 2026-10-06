#include "Application/ApplicationLaunchOptions.h"
#include "Application/Content/DesktopApplicationContent.h"
#include "Application/SelfTest/SelfTestRunner.h"
#include "GGLabFoundation/String/StringUtils.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <string>
#include <system_error>

namespace gglab
{
	namespace
	{
		std::optional<ApplicationStartupDemo> ParseDemo(std::string_view value) noexcept
		{
			if (utils::EqualsAsciiIgnoreCase(value, "start") ||
				utils::EqualsAsciiIgnoreCase(value, "demo.start"))
			{
				return ApplicationStartupDemo::Start;
			}
			if (utils::EqualsAsciiIgnoreCase(value, "island") ||
				utils::EqualsAsciiIgnoreCase(value, "demo.playground.island"))
			{
				return ApplicationStartupDemo::Island;
			}
			if (utils::EqualsAsciiIgnoreCase(value, "atrium") ||
				utils::EqualsAsciiIgnoreCase(value, "demo.playground.coastalatrium"))
			{
				return ApplicationStartupDemo::CoastalAtrium;
			}
			if (utils::EqualsAsciiIgnoreCase(value, "lab") ||
				utils::EqualsAsciiIgnoreCase(value, "labhost") ||
				utils::EqualsAsciiIgnoreCase(value, "demo.labhost"))
			{
				return ApplicationStartupDemo::LabHost;
			}
			return std::nullopt;
		}

		template <typename T>
		[[nodiscard]] std::optional<T> ParseNumber(std::string_view value) noexcept
		{
			T result{};
			const char* end = value.data() + value.size();
			const auto [last, error] = std::from_chars(value.data(), end, result);
			if (value.empty() || error != std::errc{} || last != end)
			{
				return std::nullopt;
			}
			return result;
		}

		[[nodiscard]] bool IsValidSessionId(std::string_view value) noexcept
		{
			return !value.empty() && value.size() <= 64 &&
				std::ranges::all_of(value, [](char character)
					{
						return (character >= 'a' && character <= 'z') ||
							(character >= 'A' && character <= 'Z') ||
							(character >= '0' && character <= '9') || character == '-' ||
							character == '_';
					});
		}

		// Parses "<width>x<height>".
		[[nodiscard]] bool ParseWindowSize(
			std::string_view value, uint32_t& outWidth, uint32_t& outHeight) noexcept
		{
			constexpr uint32_t minimumExtent = 64;
			constexpr uint32_t maximumExtent = 16384;
			const size_t separator = value.find_first_of("xX");
			if (separator == std::string_view::npos)
			{
				return false;
			}
			const std::optional<uint32_t> width =
				ParseNumber<uint32_t>(value.substr(0, separator));
			const std::optional<uint32_t> height =
				ParseNumber<uint32_t>(value.substr(separator + 1));
			if (!width || !height || *width < minimumExtent || *height < minimumExtent ||
				*width > maximumExtent || *height > maximumExtent)
			{
				return false;
			}
			outWidth = *width;
			outHeight = *height;
			return true;
		}
	}

	ApplicationLaunchParseResult ParseApplicationLaunchOptions(
		std::span<const std::string_view> arguments) noexcept
	{
		ApplicationLaunchParseResult result{};
		bool demoSpecified = false;
		// Capture detail options are collected first and require --capture-on-ready.
		ApplicationCaptureOnReadyOptions capture{};
		std::optional<std::filesystem::path> captureOutputDirectory;
		bool captureDetailSpecified = false;
		const auto requireValue = [&](size_t& index, std::string_view option) noexcept
			{
				if (++index >= arguments.size() || arguments[index].empty())
				{
					result.m_Error = std::format("Option '{}' requires a value.", option);
					return false;
				}
				return true;
			};
		for (size_t index = 0; index < arguments.size(); ++index)
		{
			const std::string_view argument = arguments[index];
			if (argument == "--help" || argument == "-h")
			{
				result.m_ShowHelp = true;
				continue;
			}
			if (argument == "--relative-mouse")
			{
				result.m_Options.m_StartWithRelativeMouse = true;
				continue;
			}
			if (argument == "--state-root")
			{
				if (!result.m_Options.m_StateRoot.empty() || ++index >= arguments.size())
				{
					result.m_Error = "Option '--state-root' requires one absolute directory and may only be specified once.";
					return result;
				}
				result.m_Options.m_StateRoot = std::filesystem::path(arguments[index]);
				if (!result.m_Options.m_StateRoot.is_absolute())
				{
					result.m_Error = "Option '--state-root' requires an absolute directory.";
					return result;
				}
				continue;
			}
			if (argument == "--no-devtools")
			{
				if (result.m_Options.m_DisableDevelopmentTools)
				{
					result.m_Error = "Option '--no-devtools' may only be specified once.";
					return result;
				}
				result.m_Options.m_DisableDevelopmentTools = true;
				continue;
			}
			if (argument == "--rhi")
			{
				if (result.m_Options.m_RhiBackendSpecified)
				{
					result.m_Error = "Option '--rhi' may only be specified once.";
					return result;
				}
				if (++index >= arguments.size() || arguments[index].empty())
				{
					result.m_Error = "Option '--rhi' requires a backend name ('dx12' or 'vulkan').";
					return result;
				}
				if (utils::EqualsAsciiIgnoreCase(arguments[index], "dx12"))
				{
					result.m_Options.m_RhiBackend = RHIBackendType::DX12;
				}
				else if (utils::EqualsAsciiIgnoreCase(arguments[index], "vulkan"))
				{
					result.m_Options.m_RhiBackend = RHIBackendType::Vulkan;
				}
				else
				{
					result.m_Error = std::format(
						"Unknown RHI backend '{}'. Expected 'dx12' or 'vulkan'.",
						arguments[index]);
					return result;
				}
				result.m_Options.m_RhiBackendSpecified = true;
				continue;
			}
			if (argument == "--list-adapters")
			{
				if (result.m_Options.m_ListAdapters)
				{
					result.m_Error = "Option '--list-adapters' may only be specified once.";
					return result;
				}
				result.m_Options.m_ListAdapters = true;
				continue;
			}
			if (argument == "--adapter")
			{
				if (result.m_Options.m_AdapterSelector)
				{
					result.m_Error = "Option '--adapter' may only be specified once.";
					return result;
				}
				if (++index >= arguments.size() || arguments[index].empty())
				{
					result.m_Error =
						"Option '--adapter' requires an enumeration index or identity prefix.";
					return result;
				}
				result.m_Options.m_AdapterSelector = std::string(arguments[index]);
				continue;
			}
			if (argument == "--demo")
			{
				if (demoSpecified)
				{
					result.m_Error = "Option '--demo' may only be specified once.";
					return result;
				}
				if (++index >= arguments.size())
				{
					result.m_Error = "Option '--demo' requires a value.";
					return result;
				}
				const auto demo = ParseDemo(arguments[index]);
				if (!demo)
				{
					result.m_Error =
						std::format("Unknown demo '{}'. Expected 'start', 'island', 'atrium', or 'lab'.",
							arguments[index]);
					return result;
				}
				result.m_Options.m_StartupDemo = *demo;
				demoSpecified = true;
				continue;
			}
			if (argument == "--lab")
			{
				if (result.m_Options.m_StartupLabId)
				{
					result.m_Error = "Option '--lab' may only be specified once.";
					return result;
				}
				if (++index >= arguments.size() || arguments[index].empty())
				{
					result.m_Error = "Option '--lab' requires a non-empty Lab ID.";
					return result;
				}
				result.m_Options.m_StartupLabId = std::string(arguments[index]);
				continue;
			}
			if (argument == "--self-test")
			{
				if (result.m_Options.m_SelfTestSelection)
				{
					result.m_Error = "Option '--self-test' may only be specified once.";
					return result;
				}
				if (++index >= arguments.size() || arguments[index].empty())
				{
					result.m_Error = "Option '--self-test' requires a non-empty suite ID.";
					return result;
				}
				if (!IsApplicationSelfTestSelectionValid(arguments[index]))
				{
					result.m_Error = std::format("Unknown self-test selection '{}'.", arguments[index]);
					return result;
				}
				result.m_Options.m_SelfTestSelection = std::string(arguments[index]);
				continue;
			}

			if (argument == "--hidden")
			{
				if (result.m_Options.m_Hidden)
				{
					result.m_Error = "Option '--hidden' may only be specified once.";
					return result;
				}
				result.m_Options.m_Hidden = true;
				continue;
			}
			if (argument == "--window-size")
			{
				if (result.m_Options.m_WindowSizeSpecified)
				{
					result.m_Error = "Option '--window-size' may only be specified once.";
					return result;
				}
				if (!requireValue(index, argument))
				{
					return result;
				}
				if (!ParseWindowSize(arguments[index], result.m_Options.m_WindowWidth,
					result.m_Options.m_WindowHeight))
				{
					result.m_Error = std::format("Option '--window-size' expects "
						"<width>x<height> between 64 and 16384, got '{}'.", arguments[index]);
					return result;
				}
				result.m_Options.m_WindowSizeSpecified = true;
				continue;
			}
			if (argument == "--fixed-delta-time")
			{
				if (result.m_Options.m_FixedDeltaTimeSeconds)
				{
					result.m_Error = "Option '--fixed-delta-time' may only be specified once.";
					return result;
				}
				if (!requireValue(index, argument))
				{
					return result;
				}
				const std::optional<double> seconds = ParseNumber<double>(arguments[index]);
				if (!seconds || !(*seconds > 0.0 && *seconds <= 1.0))
				{
					result.m_Error = std::format(
						"Option '--fixed-delta-time' expects seconds in (0, 1], got '{}'.",
						arguments[index]);
					return result;
				}
				result.m_Options.m_FixedDeltaTimeSeconds = *seconds;
				continue;
			}
			if (argument == "--capture-on-ready")
			{
				if (captureOutputDirectory)
				{
					result.m_Error = "Option '--capture-on-ready' may only be specified once.";
					return result;
				}
				if (!requireValue(index, argument))
				{
					return result;
				}
				captureOutputDirectory = std::filesystem::path(arguments[index]);
				if (!captureOutputDirectory->is_absolute())
				{
					result.m_Error = "Option '--capture-on-ready' requires an absolute directory.";
					return result;
				}
				continue;
			}
			if (argument == "--capture-source")
			{
				if (!requireValue(index, argument))
				{
					return result;
				}
				if (utils::EqualsAsciiIgnoreCase(arguments[index], "scene"))
				{
					capture.m_Source = FrameCaptureSource::Scene;
				}
				else if (utils::EqualsAsciiIgnoreCase(arguments[index], "composited"))
				{
					capture.m_Source = FrameCaptureSource::Composited;
				}
				else
				{
					result.m_Error = std::format(
						"Unknown capture source '{}'. Expected 'scene' or 'composited'.",
						arguments[index]);
					return result;
				}
				captureDetailSpecified = true;
				continue;
			}
			if (argument == "--capture-settle-frames")
			{
				if (!requireValue(index, argument))
				{
					return result;
				}
				const std::optional<uint32_t> frames = ParseNumber<uint32_t>(arguments[index]);
				if (!frames || *frames > 10000)
				{
					result.m_Error = std::format(
						"Option '--capture-settle-frames' expects 0 to 10000, got '{}'.",
						arguments[index]);
					return result;
				}
				capture.m_SettleFrames = *frames;
				captureDetailSpecified = true;
				continue;
			}
			if (argument == "--capture-label")
			{
				if (!requireValue(index, argument))
				{
					return result;
				}
				capture.m_Label = std::string(arguments[index]);
				captureDetailSpecified = true;
				continue;
			}
			if (argument == "--capture-view")
			{
				if (!requireValue(index, argument))
				{
					return result;
				}
				capture.m_ReferenceViewId = std::string(arguments[index]);
				captureDetailSpecified = true;
				continue;
			}
			if (argument == "--capture-timeout")
			{
				if (!requireValue(index, argument))
				{
					return result;
				}
				const std::optional<double> seconds = ParseNumber<double>(arguments[index]);
				if (!seconds || !(*seconds > 0.0 && *seconds <= 86400.0))
				{
					result.m_Error = std::format(
						"Option '--capture-timeout' expects seconds in (0, 86400], got '{}'.",
						arguments[index]);
					return result;
				}
				capture.m_TimeoutSeconds = *seconds;
				captureDetailSpecified = true;
				continue;
			}

			if (argument == "--output-log")
			{
				if (!result.m_Options.m_OutputLog.empty())
				{
					result.m_Error = "Option '--output-log' may only be specified once.";
					return result;
				}
				if (!requireValue(index, argument))
				{
					return result;
				}
				result.m_Options.m_OutputLog = std::filesystem::path(arguments[index]);
				if (!result.m_Options.m_OutputLog.is_absolute())
				{
					result.m_Error = "Option '--output-log' requires an absolute file path.";
					return result;
				}
				continue;
			}
			if (argument == "--session")
			{
				if (result.m_Options.m_SessionId)
				{
					result.m_Error = "Option '--session' may only be specified once.";
					return result;
				}
				if (!requireValue(index, argument))
				{
					return result;
				}
				if (!IsValidSessionId(arguments[index]))
				{
					result.m_Error = std::format("Option '--session' expects 1 to 64 characters "
						"from [A-Za-z0-9_-], got '{}'.", arguments[index]);
					return result;
				}
				result.m_Options.m_SessionId = std::string(arguments[index]);
				continue;
			}
			if (argument == "--idle-timeout")
			{
				if (result.m_Options.m_IdleTimeoutSpecified)
				{
					result.m_Error = "Option '--idle-timeout' may only be specified once.";
					return result;
				}
				if (!requireValue(index, argument))
				{
					return result;
				}
				const std::optional<double> seconds = ParseNumber<double>(arguments[index]);
				if (!seconds || !(*seconds > 0.0 && *seconds <= 604800.0))
				{
					result.m_Error = std::format(
						"Option '--idle-timeout' expects seconds in (0, 604800], got '{}'.",
						arguments[index]);
					return result;
				}
				result.m_Options.m_IdleTimeoutSeconds = *seconds;
				result.m_Options.m_IdleTimeoutSpecified = true;
				continue;
			}

			result.m_Error = std::format("Unknown option '{}'.", argument);
			return result;
		}

		if (captureOutputDirectory)
		{
			capture.m_OutputDirectory = *captureOutputDirectory;
			result.m_Options.m_CaptureOnReady = std::move(capture);
		}
		else if (captureDetailSpecified)
		{
			result.m_Error = "Capture options require '--capture-on-ready'.";
			return result;
		}
		if (result.m_Options.m_IdleTimeoutSpecified && !result.m_Options.m_SessionId)
		{
			result.m_Error = "Option '--idle-timeout' requires '--session'.";
			return result;
		}
		if (result.m_Options.m_Hidden && result.m_Options.m_StartWithRelativeMouse)
		{
			result.m_Error = "Option '--hidden' cannot be combined with '--relative-mouse'.";
			return result;
		}

		if (result.m_Options.m_StartupLabId)
		{
			if (demoSpecified && result.m_Options.m_StartupDemo != ApplicationStartupDemo::LabHost)
			{
				result.m_Error =
					"Option '--lab' cannot be combined with a non-LabHost '--demo' value.";
				return result;
			}
			result.m_Options.m_StartupDemo = ApplicationStartupDemo::LabHost;
		}
		if (result.m_Options.m_SelfTestSelection &&
			(demoSpecified || result.m_Options.m_StartupLabId ||
				result.m_Options.m_StartWithRelativeMouse ||
				result.m_Options.m_DisableDevelopmentTools ||
				result.m_Options.m_RhiBackendSpecified || result.m_Options.m_ListAdapters ||
				result.m_Options.m_AdapterSelector || result.m_Options.m_Hidden ||
				result.m_Options.m_WindowSizeSpecified ||
				result.m_Options.m_FixedDeltaTimeSeconds || result.m_Options.m_CaptureOnReady ||
				result.m_Options.m_SessionId))
		{
			result.m_Error =
				"Option '--self-test' cannot be combined with interactive startup options.";
			return result;
		}
		if (result.m_Options.m_AdapterSelector && result.m_Options.m_ListAdapters)
		{
			result.m_Error = "Option '--adapter' cannot be combined with '--list-adapters'.";
			return result;
		}
		if (result.m_Options.m_ListAdapters && result.m_Options.m_RhiBackendSpecified &&
			result.m_Options.m_RhiBackend != RHIBackendType::Vulkan)
		{
			result.m_Error =
				"Option '--list-adapters' requires the Vulkan backend and cannot be combined with '--rhi dx12'.";
			return result;
		}
		if (result.m_Options.m_AdapterSelector &&
			(!result.m_Options.m_RhiBackendSpecified ||
				result.m_Options.m_RhiBackend != RHIBackendType::Vulkan))
		{
			result.m_Error =
				"Option '--adapter' requires an explicit '--rhi vulkan'.";
			return result;
		}
		return result;
	}

	std::string_view GetApplicationLaunchUsage() noexcept
	{
		return "Usage: GraphicsGadgetLab.exe [options]\n"
			"\n"
			"Options:\n"
			"  --demo <start|island|atrium|lab> Select the startup demo.\n"
			"  --lab <stable-lab-id>           Start LabHost with the requested Lab.\n"
			"  --relative-mouse                Start with a captured cursor for camera control.\n"
			"                                  Default: visible, uncaptured cursor (Absolute).\n"
			"  --state-root <absolute-path>    Store artifacts, caches and settings outside deployed inputs.\n"
			"  --no-devtools                   Disable optional desktop development tooling.\n"
			"  --rhi <dx12|vulkan>             Select the RHI backend (default: dx12).\n"
			"                                  Explicit 'vulkan' never falls back to DX12.\n"
			"  --list-adapters                 Enumerate Vulkan adapters with profile\n"
			"                                  evaluation and exit.\n"
			"  --adapter <index|prefix>        Select a Vulkan adapter by enumeration\n"
			"                                  index or device UUID/name prefix.\n"
			"                                  Requires --rhi vulkan.\n"
			"  --window-size <width>x<height>  Main window client size (default: 1920x1080).\n"
			"  --hidden                        Never show or activate the window; ignore input.\n"
			"                                  Implies --fixed-delta-time 1/60 unless set.\n"
			"  --fixed-delta-time <seconds>    Advance simulation time by a fixed step per frame.\n"
			"  --capture-on-ready <abs-dir>    Capture once after readiness and settling, then\n"
			"                                  exit (0: written, 2: failed, 3: timed out).\n"
			"  --capture-source <scene|composited>  Capture tap (default: scene).\n"
			"  --capture-settle-frames <n>     Ready frames before the capture (default: 8).\n"
			"  --capture-label <text>          Label recorded in the capture metadata.\n"
			"  --capture-view <id>             Restore this camera reference view first.\n"
			"  --capture-timeout <seconds>     Limit from the first frame (default: 120).\n"
			"  --session <id>                  Serve the session control protocol on\n"
			"                                  \\.\\pipe\\gglab-session-<id> ([A-Za-z0-9_-]).\n"
			"  --idle-timeout <seconds>        Exit a session after this long without a\n"
			"                                  control request (default: 900).\n"
			"  --output-log <absolute-file>    Write stdout and stderr to this file.\n"
			"  --self-test <suite-id|all>      Run one or all headless self-test suites.\n"
			"                                  Available: artifact-cache, asset-data,\n"
			"                                  publication-accounting, rendering-contracts,\n"
			"                                  napa-voxel, vulkan-contracts, all.\n"
			"  --help, -h                      Show this help text.\n"
			"Requires the Vulkan SDK 1.3.296 for Vulkan backend builds\n"
			"(GGLAB_ENABLE_VULKAN=1, the default).\n";
	}
}
