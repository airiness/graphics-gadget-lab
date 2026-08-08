#include "Core/Precompiled.h"
#include "Application/RenderingStartup.h"
#include "Core/Log/Logger.h"
#include "Graphics/RHI/RHIFormat.h"
#if GGLAB_ENABLE_VULKAN
#include "Graphics/RHI/Vulkan/VulkanBootstrap.h"
#endif

#include <array>
#include <charconv>
#include <cstdint>
#include <string_view>

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

		[[nodiscard]] VulkanBootstrapOptions MakeVulkanBootstrapOptions(
			const ApplicationLaunchOptions& options, HWND hwnd) noexcept
		{
			VulkanBootstrapOptions bootstrapOptions{};
			bootstrapOptions.m_HInstance = GetModuleHandle(nullptr);
			bootstrapOptions.m_Hwnd = hwnd;
			// Validation is a Debug-build policy: Debug requests the Khronos
			// validation layer when available, Release never requests it.
#if defined(BUILD_DEBUG)
			bootstrapOptions.m_RequestValidation = true;
#else
			bootstrapOptions.m_RequestValidation = false;
#endif
			bootstrapOptions.m_SelectionRequest =
				MakeVulkanSelectionRequest(options.m_AdapterSelector);
			return bootstrapOptions;
		}

		void LogQualificationInfo(const std::string& message) noexcept
		{
			if (auto& logger = Logger::GetLogger(Logger::LoggerType::Application))
			{
				logger->info("{}", message);
			}
		}

		void LogQualificationError(const std::string& message) noexcept
		{
			if (auto& logger = Logger::GetLogger(Logger::LoggerType::Application))
			{
				logger->error("{}", message);
			}
		}

		[[nodiscard]] std::string_view PresentModeName(VkPresentModeKHR mode) noexcept
		{
			switch (mode)
			{
			case VK_PRESENT_MODE_FIFO_KHR:
				return "FIFO";
			case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
				return "FIFO_RELAXED";
			case VK_PRESENT_MODE_MAILBOX_KHR:
				return "MAILBOX";
			case VK_PRESENT_MODE_IMMEDIATE_KHR:
				return "IMMEDIATE";
			default:
				return "other";
			}
		}

		struct QualificationFrameStats
		{
			uint32_t m_NormalFrames = 0;
			uint32_t m_AbortFrames = 0;
			uint32_t m_RecreateCount = 0;
			uint32_t m_MismatchedFrames = 0;
			uint32_t m_SuboptimalCount = 0;
		};

		// Deterministic per-step clear colors so presented frames are easy to
		// distinguish: red, green, blue, white cycling.
		[[nodiscard]] std::array<float, 4> StepColor(uint32_t step) noexcept
		{
			constexpr std::array<std::array<float, 4>, 4> palette{
				std::array<float, 4>{1.0f, 0.2f, 0.2f, 1.0f},
				std::array<float, 4>{0.2f, 1.0f, 0.2f, 1.0f},
				std::array<float, 4>{0.2f, 0.3f, 1.0f, 1.0f},
				std::array<float, 4>{0.9f, 0.9f, 0.9f, 1.0f},
			};
			return palette[step % palette.size()];
		}

		// Runs one frame of the script and logs the two-index domains plus
		// the semaphore identities that own each of them.
		[[nodiscard]] bool RunQualificationStep(VulkanFrameRuntime& runtime, uint32_t step,
			bool abort, QualificationFrameStats& stats) noexcept
		{
			VulkanBeginFrameResult begin = runtime.BeginFrame();
			if (!begin.m_Acquired)
			{
				LogQualificationError(
					std::format("qualify[{:03d}] BeginFrame failed to acquire an image.", step));
				return false;
			}
			if (begin.m_RecreatePending)
			{
				++stats.m_SuboptimalCount;
			}
			if (begin.m_FrameSlotIndex != begin.m_BackBufferIndex)
			{
				++stats.m_MismatchedFrames;
			}
			if (abort)
			{
				++stats.m_AbortFrames;
				runtime.AbortFrame(begin.m_FrameSlotIndex, begin.m_BackBufferIndex);
			}
			else
			{
				++stats.m_NormalFrames;
				runtime.EndFrame(begin.m_FrameSlotIndex, begin.m_BackBufferIndex,
					StepColor(step));
			}
			LogQualificationInfo(std::format(
				"qualify[{:03d}] {:5s} frameSlot={} backBuffer={} imageAvailable=0x{:016x} "
				"renderingFinished=0x{:016x} timeline={}",
				step, abort ? "abort" : "normal", begin.m_FrameSlotIndex,
				begin.m_BackBufferIndex,
				reinterpret_cast<uint64_t>(
					runtime.GetImageAvailableSemaphore(begin.m_FrameSlotIndex)),
				reinterpret_cast<uint64_t>(
					runtime.GetRenderingFinishedSemaphore(begin.m_BackBufferIndex)),
				runtime.GetTimelineSignalValue()));
			return true;
		}

		[[nodiscard]] bool RunQualificationRecreate(VulkanFrameRuntime& runtime, HWND hwnd,
			bool vsync, QualificationFrameStats& stats) noexcept
		{
			RECT clientRect{};
			if (!GetClientRect(hwnd, &clientRect))
			{
				LogQualificationError("GetClientRect failed before swapchain recreate.");
				return false;
			}
			const uint32_t width =
				static_cast<uint32_t>(std::max<LONG>(clientRect.right - clientRect.left, 0));
			const uint32_t height =
				static_cast<uint32_t>(std::max<LONG>(clientRect.bottom - clientRect.top, 0));
			if (width == 0 || height == 0)
			{
				LogQualificationError(
					"Swapchain recreate requested at a zero drawable extent; skipping.");
				return false;
			}

			std::string error;
			if (!runtime.RecreateSwapChain(width, height, vsync, error))
			{
				LogQualificationError(std::format("Swapchain recreate failed: {}", error));
				return false;
			}
			++stats.m_RecreateCount;
			const auto& swapChain = runtime.GetSwapChain();
			LogQualificationInfo(std::format(
				"qualify[recreate] {}x{} vsync={} presentMode={} format={} images={}",
				width, height, vsync ? "on" : "off", PresentModeName(swapChain.GetPresentMode()),
				GetRHIFormatInfo(swapChain.GetFormat()).m_Name, swapChain.GetImageCount()));
			return true;
		}

		[[nodiscard]] bool ResizeQualificationWindow(HWND hwnd, uint32_t width,
			uint32_t height) noexcept
		{
			// The window is owned by the application platform layer; this is
			// the only place the qualification path touches it, to drive a
			// real WM_SIZE-style resize without a message pump.
			const BOOL moved = SetWindowPos(hwnd, nullptr, 0, 0,
				static_cast<int>(width), static_cast<int>(height),
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			if (!moved)
			{
				LogQualificationError(std::format(
					"SetWindowPos({}x{}) failed with error {}.", width, height,
					static_cast<uint32_t>(GetLastError())));
				return false;
			}
			return true;
		}

		// Runs the minimal-frame qualification script: normal presents,
		// first-image abort, already-presented abort, normal/abort
		// alternation, continuous abort, resize, minimize/restore and VSync
		// switching. Every step keeps the partial application command buffer
		// unsubmitted; AbortFrame uses the dedicated minimal command buffer.
		[[nodiscard]] int RunVulkanQualificationFrames(
			VulkanFrameRuntime& runtime, HWND hwnd) noexcept
		{
			QualificationFrameStats stats;
			uint32_t step = 0;
			const auto runNormal = [&runtime, &stats, &step]()
				{
					return RunQualificationStep(runtime, step++, false, stats);
				};
			const auto runAbort = [&runtime, &stats, &step]()
				{
					return RunQualificationStep(runtime, step++, true, stats);
				};

			LogQualificationInfo("Vulkan minimal-frame qualification started.");

			// Phase 1: continuous normal presents.
			for (uint32_t i = 0; i < 4; ++i)
			{
				if (!runNormal())
				{
					return 1;
				}
			}
			// Phase 2: continuous aborts.
			for (uint32_t i = 0; i < 3; ++i)
			{
				if (!runAbort())
				{
					return 1;
				}
			}
			// Phase 3: normal/abort alternation.
			for (uint32_t i = 0; i < 4; ++i)
			{
				if (!runNormal() || !runAbort())
				{
					return 1;
				}
			}
			// Phase 4: grouped frames ending in aborts.
			for (uint32_t i = 0; i < 3; ++i)
			{
				if (!runNormal())
				{
					return 1;
				}
			}
			if (!runAbort() || !runNormal())
			{
				return 1;
			}
			for (uint32_t i = 0; i < 3; ++i)
			{
				if (!runAbort() || !runNormal())
				{
					return 1;
				}
			}

			// Phase 5: real window resize. Every recreate is followed by an
			// abort, so a freshly created swapchain exercises the first-image
			// Undefined -> Present abort path.
			constexpr std::array<std::pair<uint32_t, uint32_t>, 3> resizeSizes{
				std::pair{ 1280u, 720u },
				std::pair{ 2560u, 1440u },
				std::pair{ 1920u, 1080u },
			};
			for (const auto& [resizeWidth, resizeHeight] : resizeSizes)
			{
				if (!ResizeQualificationWindow(hwnd, resizeWidth, resizeHeight))
				{
					return 1;
				}
				if (!RunQualificationRecreate(runtime, hwnd, runtime.GetVsync(), stats))
				{
					return 1;
				}
				if (!runAbort() || !runNormal() || !runNormal())
				{
					return 1;
				}
			}

			// Phase 6: VSync on/off. The actual present mode is logged after
			// each recreate.
			if (!RunQualificationRecreate(runtime, hwnd, true, stats))
			{
				return 1;
			}
			for (uint32_t i = 0; i < 2; ++i)
			{
				if (!runNormal())
				{
					return 1;
				}
			}
			if (!RunQualificationRecreate(runtime, hwnd, false, stats))
			{
				return 1;
			}
			for (uint32_t i = 0; i < 2; ++i)
			{
				if (!runNormal())
				{
					return 1;
				}
			}

			// Phase 7: minimize/restore. A minimized window has a zero
			// drawable extent: BeginFrame must never be called, no zero-size
			// swapchain is created, and restore recreates at the real extent.
			ShowWindow(hwnd, SW_MINIMIZE);
			{
				RECT minimizedRect{};
				GetClientRect(hwnd, &minimizedRect);
				if (minimizedRect.right - minimizedRect.left != 0 ||
					minimizedRect.bottom - minimizedRect.top != 0)
				{
					LogQualificationError(
						"Minimized window did not report a zero drawable extent.");
					return 1;
				}
				LogQualificationInfo(
					"qualify[minimize] drawable extent is 0x0; BeginFrame skipped.");
			}
			ShowWindow(hwnd, SW_RESTORE);
			if (!RunQualificationRecreate(runtime, hwnd, runtime.GetVsync(), stats))
			{
				return 1;
			}
			for (uint32_t i = 0; i < 2; ++i)
			{
				if (!runNormal())
				{
					return 1;
				}
			}

			runtime.WaitIdle();
			const auto& swapChain = runtime.GetSwapChain();
			LogQualificationInfo(std::format(
				"qualify summary: normal={} abort={} recreate={} mismatchedFrames={} "
				"suboptimal={} timeline={}",
				stats.m_NormalFrames, stats.m_AbortFrames, stats.m_RecreateCount,
				stats.m_MismatchedFrames, stats.m_SuboptimalCount,
				runtime.GetTimelineSignalValue()));
			LogQualificationInfo(std::format(
				"qualify swapchain: format={} extent={}x{} presentMode={} vsync={} images={} "
				"frameSlots={}",
				GetRHIFormatInfo(swapChain.GetFormat()).m_Name, swapChain.GetWidth(),
				swapChain.GetHeight(), PresentModeName(swapChain.GetPresentMode()),
				runtime.GetVsync() ? "on" : "off", swapChain.GetImageCount(),
				runtime.GetFrameSlotCount()));
			LogQualificationInfo("Vulkan minimal-frame qualification finished.");
			return 0;
		}
#endif
	}

	int RunRenderingStartupPath(
		const ApplicationLaunchOptions& options, HWND hwnd) noexcept
	{
#if GGLAB_ENABLE_VULKAN
		if (options.m_ListAdapters)
		{
			// Inspection-only: enumerate, evaluate and log every adapter,
			// then exit without creating a frame runtime.
			VulkanBootstrapReport report;
			return RunVulkanBootstrap(MakeVulkanBootstrapOptions(options, hwnd), report);
		}

		RECT clientRect{};
		if (!GetClientRect(hwnd, &clientRect) ||
			clientRect.right - clientRect.left == 0 ||
			clientRect.bottom - clientRect.top == 0)
		{
			LogQualificationError("Vulkan startup requires a nonzero window drawable extent.");
			return 1;
		}

		VulkanBootstrapRuntimeCreateInfo createInfo{};
		createInfo.m_BootstrapOptions = MakeVulkanBootstrapOptions(options, hwnd);
		createInfo.m_FrameSlotCount = 2;
		createInfo.m_RequestedFormat = RHIFormat::R8G8B8A8Unorm;
		createInfo.m_Vsync = false;
		createInfo.m_Width =
			static_cast<uint32_t>(clientRect.right - clientRect.left);
		createInfo.m_Height =
			static_cast<uint32_t>(clientRect.bottom - clientRect.top);

		VulkanBootstrapRuntimeResult result = CreateVulkanBootstrapRuntime(createInfo);
		if (!result.Succeeded())
		{
			LogQualificationError(std::format(
				"Vulkan bootstrap runtime creation failed: {}", result.m_Error));
			return 1;
		}
		LogQualificationInfo(std::format(
			"Vulkan qualification on adapter [{}] '{}' (validation={}).",
			result.m_SelectedSnapshot.m_Identity.m_EnumerationIndex,
			result.m_SelectedSnapshot.m_Identity.m_DeviceName,
			result.m_HasDebugMessenger ? "enabled" : "disabled"));

		const int exitCode =
			RunVulkanQualificationFrames(*result.m_FrameRuntime, hwnd);
		return exitCode;
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
