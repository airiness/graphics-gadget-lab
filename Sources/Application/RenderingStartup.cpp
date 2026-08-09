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

		// Outcome of a swapchain recreation request. Recreated means the
		// swapchain was rebuilt at the real drawable extent; Suspended means
		// the drawable extent is zero (never create a zero-size swapchain,
		// never BeginFrame); Failed is an explicit error. The distinction
		// matters because a suspended window is an environment state, not a
		// failure: a real main loop waits for restore instead of retrying.
		enum class VulkanQualificationRecreateOutcome : uint8_t
		{
			Recreated,
			Suspended,
			Failed,
		};

		[[nodiscard]] VulkanQualificationRecreateOutcome RunQualificationRecreate(
			VulkanFrameRuntime& runtime, HWND hwnd, bool vsync,
			QualificationFrameStats& stats) noexcept;

		// Script-level helper: a recreate must actually happen for the script
		// to continue; both Suspended and Failed stop the deterministic
		// qualification script (it cannot drive frames while suspended).
		[[nodiscard]] bool RunQualificationRecreateChecked(VulkanFrameRuntime& runtime,
			HWND hwnd, bool vsync, QualificationFrameStats& stats) noexcept
		{
			switch (RunQualificationRecreate(runtime, hwnd, vsync, stats))
			{
			case VulkanQualificationRecreateOutcome::Recreated:
				return true;
			case VulkanQualificationRecreateOutcome::Suspended:
			case VulkanQualificationRecreateOutcome::Failed:
				return false;
			}
			return false;
		}

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
		// the semaphore identities that own each of them. An OUT_OF_DATE
		// acquire is retried once after recreating with the real window
		// extent, and a SUBOPTIMAL acquire or a SUBOPTIMAL/OUT_OF_DATE
		// present schedules a recreation at the next safe point (after the
		// frame transaction completes): the platform owns the drawable
		// extent, the runtime only owns the recreation mechanics.
		[[nodiscard]] bool RunQualificationStep(VulkanFrameRuntime& runtime, HWND hwnd,
			uint32_t step, bool abort, QualificationFrameStats& stats) noexcept;

		[[nodiscard]] bool RunQualificationStep(VulkanFrameRuntime& runtime, HWND hwnd,
			uint32_t step, bool abort, QualificationFrameStats& stats) noexcept
		{
			VulkanBeginFrameResult begin = runtime.BeginFrame();
			if (begin.m_Status == VulkanAcquireOutcome::OutOfDate)
			{
				LogQualificationInfo(std::format(
					"qualify[{:03d}] acquire OUT_OF_DATE; recreating with the real extent and "
					"retrying once.",
					step));
				if (!RunQualificationRecreateChecked(runtime, hwnd, runtime.GetVsync(), stats))
				{
					return false;
				}
				begin = runtime.BeginFrame();
			}
			if (!begin.IsAcquired())
			{
				LogQualificationError(std::format(
					"qualify[{:03d}] BeginFrame failed (status={}, result={}).", step,
					static_cast<int>(begin.m_Status), static_cast<int>(begin.m_Result)));
				return false;
			}
			// A SUBOPTIMAL acquire still hands over a valid image: the frame
			// transaction runs to completion first, then the recreation is
			// consumed at the safe point below.
			const bool acquireRecreatePending = begin.m_RecreatePending;
			if (acquireRecreatePending)
			{
				++stats.m_SuboptimalCount;
			}
			if (begin.m_FrameSlotIndex != begin.m_BackBufferIndex)
			{
				++stats.m_MismatchedFrames;
			}
			VulkanSubmitPresentResult endResult{};
			if (abort)
			{
				++stats.m_AbortFrames;
				endResult = runtime.AbortFrame();
			}
			else
			{
				++stats.m_NormalFrames;
				endResult = runtime.EndFrame(StepColor(step));
			}
			if (endResult.m_Fatal)
			{
				LogQualificationError(std::format(
					"qualify[{:03d}] frame transaction failed (result={}).", step,
					static_cast<int>(endResult.m_Result)));
				return false;
			}
			// Consume recreation requirements at the safe point: the frame
			// transaction has completed and no frame is active. The real
			// drawable extent is queried every time; a zero extent skips the
			// recreation (and no BeginFrame happens while suspended).
			if (acquireRecreatePending || endResult.m_RecreatePending)
			{
				if (endResult.m_RecreatePending)
				{
					++stats.m_SuboptimalCount;
				}
				LogQualificationInfo(std::format(
					"qualify[{:03d}] recreation pending; recreating at the safe point.", step));
				if (!RunQualificationRecreateChecked(runtime, hwnd, runtime.GetVsync(), stats))
				{
					return false;
				}
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

		[[nodiscard]] VulkanQualificationRecreateOutcome RunQualificationRecreate(
			VulkanFrameRuntime& runtime, HWND hwnd, bool vsync,
			QualificationFrameStats& stats) noexcept
		{
			RECT clientRect{};
			if (!GetClientRect(hwnd, &clientRect))
			{
				LogQualificationError("GetClientRect failed before swapchain recreate.");
				return VulkanQualificationRecreateOutcome::Failed;
			}
			const uint32_t width =
				static_cast<uint32_t>(std::max<LONG>(clientRect.right - clientRect.left, 0));
			const uint32_t height =
				static_cast<uint32_t>(std::max<LONG>(clientRect.bottom - clientRect.top, 0));
			if (width == 0 || height == 0)
			{
				// Suspended window: never create a zero-size swapchain. The
				// caller stops driving frames until the window is restored.
				LogQualificationInfo(
					"qualify[recreate] drawable extent is zero; recreation suspended.");
				return VulkanQualificationRecreateOutcome::Suspended;
			}

			std::string error;
			if (!runtime.RecreateSwapChain(width, height, vsync, error))
			{
				LogQualificationError(std::format("Swapchain recreate failed: {}", error));
				return VulkanQualificationRecreateOutcome::Failed;
			}
			++stats.m_RecreateCount;
			const auto& swapChain = runtime.GetSwapChain();
			LogQualificationInfo(std::format(
				"qualify[recreate] {}x{} vsync={} presentMode={} format={} images={}",
				width, height, vsync ? "on" : "off", PresentModeName(swapChain.GetPresentMode()),
				GetRHIFormatInfo(swapChain.GetFormat()).m_Name, swapChain.GetImageCount()));
			return VulkanQualificationRecreateOutcome::Recreated;
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

		// Registers a deferred-retirement probe on a reserved-but-unsubmitted
		// timeline value. The gate is deterministically incomplete here; later
		// frames submit past it. The probe slot must stay occupied until
		// RunVulkanDeferredRetirementRelease runs after WaitIdle.
		[[nodiscard]] int RunVulkanDeferredRetirementProbe(
			VulkanDevice& device, VulkanFrameRuntime& runtime, uint32_t& outPendingSlot) noexcept
		{
			VulkanResourceManager& resources = device.GetResourceManager();
			// One above the last committed value is reserved, not submitted:
			// the gate is deterministically incomplete at this point, and the
			// submissions that follow this probe complete it.
			const RHIFencePoint incompletePoint(
				runtime.GetTimeline().GetRHIHandle(),
				runtime.GetTimeline().GetCurrentSignalValue() + 1);

			RHITextureHandle pending = resources.CreateTexture(
				RHIOwnedTextureCreateInfo{
					.m_Desc = RHITextureDesc{
						.m_Format = RHIFormat::R8G8B8A8Unorm,
						.m_Usage = RHITextureUsage::Sampled,
						.m_Extent = { 16, 16, 1 },
					},
					.m_InitialState = RHIResourceState{
						.m_Stages = RHIStage::PixelShader,
						.m_Access = RHIAccess::ShaderResource,
						.m_Layout = RHILayout::ShaderResource,
					},
				},
				{ .m_Domain = RHIResourceDebugDomain::Diagnostics });
			if (!pending.IsValid())
			{
				LogQualificationError("qualify resource: deferred texture creation failed.");
				return 1;
			}
			resources.RecordTextureUse(pending, incompletePoint);
			resources.DestroyTexture(pending);
			resources.RetireCompletedResources();

			RHITextureHandle probe = resources.CreateTexture(
				RHIOwnedTextureCreateInfo{
					.m_Desc = RHITextureDesc{
						.m_Format = RHIFormat::R8G8B8A8Unorm,
						.m_Usage = RHITextureUsage::Sampled,
						.m_Extent = { 16, 16, 1 },
					},
					.m_InitialState = RHIResourceState{
						.m_Stages = RHIStage::PixelShader,
						.m_Access = RHIAccess::ShaderResource,
						.m_Layout = RHILayout::ShaderResource,
					},
				},
				{ .m_Domain = RHIResourceDebugDomain::Diagnostics });
			if (!probe.IsValid() || probe.Index() == pending.Index())
			{
				LogQualificationError(
					"qualify resource: incomplete retirement gate released the slot.");
				return 1;
			}
			resources.DestroyTexture(probe);
			resources.RetireCompletedResources();
			outPendingSlot = pending.Index();
			LogQualificationInfo(std::format(
				"qualify resource: incomplete gate at timeline {} retained slot {}.",
				incompletePoint.m_Value, pending.Index()));
			return 0;
		}

		// After the submissions that followed the probe, the gate value has
		// been passed: the pending slot must now be released and reusable.
		[[nodiscard]] int RunVulkanDeferredRetirementRelease(
			VulkanDevice& device, uint32_t pendingSlot) noexcept
		{
			VulkanResourceManager& resources = device.GetResourceManager();
			resources.RetireCompletedResources();

			RHITextureHandle probe = resources.CreateTexture(
				RHIOwnedTextureCreateInfo{
					.m_Desc = RHITextureDesc{
						.m_Format = RHIFormat::R8G8B8A8Unorm,
						.m_Usage = RHITextureUsage::Sampled,
						.m_Extent = { 16, 16, 1 },
					},
					.m_InitialState = RHIResourceState{
						.m_Stages = RHIStage::PixelShader,
						.m_Access = RHIAccess::ShaderResource,
						.m_Layout = RHILayout::ShaderResource,
					},
				},
				{ .m_Domain = RHIResourceDebugDomain::Diagnostics });
			if (!probe.IsValid() || probe.Index() != pendingSlot)
			{
				LogQualificationError(
					"qualify resource: completed gate did not release the pending slot.");
				return 1;
			}
			resources.DestroyTexture(probe);
			resources.RetireCompletedResources();
			LogQualificationInfo(std::format(
				"qualify resource: deferred gate completed; slot {} was released and reused.",
				pendingSlot));
			return 0;
		}

		// Exercises the resource layer on the real device: buffer/texture
		// creation, texture views, samplers, persistent buffer mapping, use
		// tracking and deferred destruction. Every retirement gate uses the
		// committed timeline value, which is already complete, so retirement
		// drains in the same call and the next iteration must reuse the exact
		// same slots and descriptor indices.
		[[nodiscard]] int RunVulkanResourceQualification(
			VulkanDevice& device, VulkanFrameRuntime& runtime) noexcept
		{
			VulkanResourceManager& resources = device.GetResourceManager();
			const RHIFencePoint completedPoint(
				runtime.GetTimeline().GetRHIHandle(),
				runtime.GetTimeline().GetCurrentSignalValue());

			RHITextureHandle firstTexture;
			RHITextureHandle secondTexture;
			RHITextureHandle thirdTexture;
			std::array<uint32_t, 4> firstViewSlots{};
			std::array<uint32_t, 3> firstDescriptorIndices{};
			constexpr uint32_t kIterations = 24;
			for (uint32_t i = 0; i < kIterations; ++i)
			{
				// Upload/readback buffers exercise the persistent mapping
				// contract: GpuOnly is never mapped, host-visible buffers are
				// mapped at creation.
				RHIBufferHandle upload = resources.CreateBuffer(
					RHIBufferDesc{ .m_SizeInBytes = 4096,
						.m_Usage = RHIBufferUsage::CopySource,
						.m_MemoryUsage = RHIMemoryUsage::CpuToGpu },
					{ .m_Domain = RHIResourceDebugDomain::Diagnostics });
				RHIBufferHandle readback = resources.CreateBuffer(
					RHIBufferDesc{ .m_SizeInBytes = 4096,
						.m_Usage = RHIBufferUsage::CopyDest,
						.m_MemoryUsage = RHIMemoryUsage::GpuToCpu },
					{ .m_Domain = RHIResourceDebugDomain::Diagnostics });
				if (!upload.IsValid() || !readback.IsValid())
				{
					LogQualificationError("qualify resource: buffer creation failed.");
					return 1;
				}
				void* mapped = resources.MapBuffer(upload, { 0, 4096 });
				if (mapped == nullptr)
				{
					LogQualificationError("qualify resource: upload buffer mapping failed.");
					return 1;
				}
				std::memset(mapped, 0xAB, 4096);
				resources.UnmapBuffer(upload, { 0, 4096 });
				if (resources.MapBuffer(readback, { 0, 4096 }) == nullptr)
				{
					LogQualificationError("qualify resource: readback buffer mapping failed.");
					return 1;
				}
				resources.UnmapBuffer(readback, {});

				// Color texture with an SRV and a typeless depth texture with
				// a DSV exercise the view-family and aspect contracts; the
				// typeless color texture exercises the mutable-format path
				// with its restricted UNORM/SRGB view family.
				RHITextureHandle color = resources.CreateTexture(
					RHIOwnedTextureCreateInfo{
						.m_Desc = RHITextureDesc{
							.m_Format = RHIFormat::R8G8B8A8Unorm,
							.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::RenderTarget,
							.m_Extent = { 64, 64, 1 },
						},
						.m_InitialState = RHIResourceState{
							.m_Stages = RHIStage::PixelShader,
							.m_Access = RHIAccess::ShaderResource,
							.m_Layout = RHILayout::ShaderResource,
						},
					},
					{ .m_Domain = RHIResourceDebugDomain::Diagnostics });
				RHITextureHandle depth = resources.CreateTexture(
					RHIOwnedTextureCreateInfo{
						.m_Desc = RHITextureDesc{
							.m_Format = RHIFormat::R32Typeless,
							.m_Usage = RHITextureUsage::DepthStencil | RHITextureUsage::Sampled,
							.m_Extent = { 64, 64, 1 },
						},
						.m_InitialState = RHIResourceState{
							.m_Stages = RHIStage::DepthStencil,
							.m_Access = RHIAccess::DepthStencilWrite,
							.m_Layout = RHILayout::DepthStencilWrite,
						},
					},
					{ .m_Domain = RHIResourceDebugDomain::Diagnostics });
				RHITextureHandle typeless = resources.CreateTexture(
					RHIOwnedTextureCreateInfo{
						.m_Desc = RHITextureDesc{
							.m_Format = RHIFormat::R8G8B8A8Typeless,
							.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::RenderTarget,
							.m_Extent = { 32, 32, 1 },
						},
						.m_InitialState = RHIResourceState{
							.m_Stages = RHIStage::PixelShader,
							.m_Access = RHIAccess::ShaderResource,
							.m_Layout = RHILayout::ShaderResource,
						},
					},
					{ .m_Domain = RHIResourceDebugDomain::Diagnostics });
				if (!color.IsValid() || !depth.IsValid() || !typeless.IsValid())
				{
					LogQualificationError("qualify resource: texture creation failed.");
					return 1;
				}

				RHITextureViewDesc srvDesc{};
				srvDesc.m_Type = RHITextureViewType::ShaderResource;
				srvDesc.m_Dimension = RHITextureViewDimension::Texture2D;
				srvDesc.m_Format = RHIFormat::R8G8B8A8Unorm;
				const RHITextureViewHandle srv = resources.CreateTextureView(color, srvDesc);
				RHITextureViewDesc dsvDesc{};
				dsvDesc.m_Type = RHITextureViewType::DepthStencil;
				dsvDesc.m_Dimension = RHITextureViewDimension::Texture2D;
				// Depth views must name their non-typeless view format.
				dsvDesc.m_Format = RHIFormat::D32Float;
				const RHITextureViewHandle dsv = resources.CreateTextureView(depth, dsvDesc);
				RHITextureViewDesc unormView = srvDesc;
				unormView.m_Format = RHIFormat::R8G8B8A8Unorm;
				const RHITextureViewHandle unorm =
					resources.CreateTextureView(typeless, unormView);
				RHITextureViewDesc srgbView = srvDesc;
				srgbView.m_Format = RHIFormat::R8G8B8A8UnormSrgb;
				const RHITextureViewHandle srgb =
					resources.CreateTextureView(typeless, srgbView);
				RHISamplerDesc samplerDesc{};
				samplerDesc.m_AddressU = RHITextureAddressMode::Border;
				samplerDesc.m_BorderColor[3] = 1.0f;
				const RHISamplerHandle sampler = resources.CreateSampler(samplerDesc);
				if (!srv.IsValid() || !dsv.IsValid() || !unorm.IsValid() ||
					!srgb.IsValid() || !sampler.IsValid())
				{
					LogQualificationError("qualify resource: view or sampler creation failed.");
					return 1;
				}

				// Bindless contract: only shader-visible views hold a
				// descriptor index.
				const RHIDescriptorHandle srvDescriptor =
					resources.GetTextureViewDescriptor(srv);
				if (!srvDescriptor.IsValid() ||
					resources.GetTextureViewDescriptor(dsv).IsValid())
				{
					LogQualificationError(
						"qualify resource: descriptor index contract violated.");
					return 1;
				}

				if (i == 0)
				{
					firstTexture = color;
					secondTexture = depth;
					thirdTexture = typeless;
					firstViewSlots = { srv.Index(), dsv.Index(), unorm.Index(), srgb.Index() };
					firstDescriptorIndices = {
						srvDescriptor.m_Index,
						resources.GetTextureViewDescriptor(unorm).m_Index,
						resources.GetTextureViewDescriptor(srgb).m_Index,
					};
				}
				else
				{
					// After immediate retirement every slot must be recycled:
					// the new handles land in exactly the same slot sets (the
					// table reuses in LIFO order, so the concrete mapping may
					// alternate) and the bindless descriptor indices stay
					// stable as a set.
					const auto sameSlotSet = [](const std::array<uint32_t, 3>& expected,
						uint32_t a, uint32_t b, uint32_t c)
						{
							return (a == expected[0] || a == expected[1] || a == expected[2]) &&
								(b == expected[0] || b == expected[1] || b == expected[2]) &&
								(c == expected[0] || c == expected[1] || c == expected[2]) &&
								a != b && a != c && b != c;
						};
					const std::array<uint32_t, 3> textureSlots{
						color.Index(), depth.Index(), typeless.Index(),
					};
					if (!sameSlotSet(
						{ firstTexture.Index(), secondTexture.Index(), thirdTexture.Index() },
						textureSlots[0], textureSlots[1], textureSlots[2]))
					{
						LogQualificationError(
							"qualify resource: texture slots were not reused.");
						return 1;
					}
					const std::array<uint32_t, 4> viewSlots{
						srv.Index(), dsv.Index(), unorm.Index(), srgb.Index(),
					};
					for (uint32_t v = 0; v < 4; ++v)
					{
						bool found = false;
						for (uint32_t expected : firstViewSlots)
						{
							found = found || viewSlots[v] == expected;
						}
						if (!found)
						{
							LogQualificationError(
								"qualify resource: view slots were not reused.");
							return 1;
						}
					}
					const std::array<uint32_t, 3> descriptorIndices{
						srvDescriptor.m_Index,
						resources.GetTextureViewDescriptor(unorm).m_Index,
						resources.GetTextureViewDescriptor(srgb).m_Index,
					};
					// Exact set equality: every index appears in the first
					// round and the round is a permutation of it, with no
					// duplicates. Index 0 is a legal descriptor index and is
					// never used as padding.
					bool descriptorSetMatches = true;
					for (uint32_t d = 0; d < 3; ++d)
					{
						bool found = false;
						for (uint32_t expected : firstDescriptorIndices)
						{
							found = found || descriptorIndices[d] == expected;
						}
						descriptorSetMatches = descriptorSetMatches && found;
					}
					for (uint32_t expected : firstDescriptorIndices)
					{
						bool found = false;
						for (uint32_t d = 0; d < 3; ++d)
						{
							found = found || descriptorIndices[d] == expected;
						}
						descriptorSetMatches = descriptorSetMatches && found;
					}
					descriptorSetMatches = descriptorSetMatches &&
						descriptorIndices[0] != descriptorIndices[1] &&
						descriptorIndices[0] != descriptorIndices[2] &&
						descriptorIndices[1] != descriptorIndices[2];
					if (!descriptorSetMatches)
					{
						LogQualificationError(
							"qualify resource: descriptor indices were not reused.");
						return 1;
					}
				}

				resources.RecordTextureUse(color, completedPoint);
				resources.RecordBufferUse(upload, completedPoint);
				resources.DestroyTextureView(srv);
				resources.DestroyTextureView(dsv);
				resources.DestroyTextureView(unorm);
				resources.DestroyTextureView(srgb);
				resources.DestroySampler(sampler);
				resources.DestroyTexture(color);
				resources.DestroyTexture(depth);
				resources.DestroyTexture(typeless);
				resources.DestroyBuffer(upload);
				resources.DestroyBuffer(readback);
				resources.RetireCompletedResources();
			}

			// The typeless family must also serve unordered access: an SRGB
			// SRV and a UNORM UAV on the same mutable-format resource.
			{
				RHITextureHandle typelessUav = resources.CreateTexture(
					RHIOwnedTextureCreateInfo{
						.m_Desc = RHITextureDesc{
							.m_Format = RHIFormat::R8G8B8A8Typeless,
							.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess,
							.m_Extent = { 32, 32, 1 },
						},
						.m_InitialState = RHIResourceState{
							.m_Stages = RHIStage::PixelShader | RHIStage::ComputeShader,
							.m_Access = RHIAccess::ShaderResource,
							.m_Layout = RHILayout::ShaderResource,
						},
					},
					{ .m_Domain = RHIResourceDebugDomain::Diagnostics });
				if (!typelessUav.IsValid())
				{
					LogQualificationError(
						"qualify resource: typeless UAV texture creation failed.");
					return 1;
				}
				RHITextureViewDesc srgbSrvDesc{};
				srgbSrvDesc.m_Type = RHITextureViewType::ShaderResource;
				srgbSrvDesc.m_Dimension = RHITextureViewDimension::Texture2D;
				srgbSrvDesc.m_Format = RHIFormat::R8G8B8A8UnormSrgb;
				const RHITextureViewHandle srgbSrv =
					resources.CreateTextureView(typelessUav, srgbSrvDesc);
				RHITextureViewDesc unormUavDesc{};
				unormUavDesc.m_Type = RHITextureViewType::UnorderedAccess;
				unormUavDesc.m_Dimension = RHITextureViewDimension::Texture2D;
				unormUavDesc.m_Format = RHIFormat::R8G8B8A8Unorm;
				const RHITextureViewHandle unormUav =
					resources.CreateTextureView(typelessUav, unormUavDesc);
				if (!srgbSrv.IsValid() || !unormUav.IsValid() ||
					!resources.GetTextureViewDescriptor(unormUav).IsValid())
				{
					LogQualificationError(
						"qualify resource: typeless UAV family creation failed.");
					return 1;
				}
				resources.DestroyTextureView(srgbSrv);
				resources.DestroyTextureView(unormUav);
				resources.DestroyTexture(typelessUav);
				resources.RetireCompletedResources();
				LogQualificationInfo(
					"qualify resource: typeless UAV family (SRGB SRV + UNORM UAV) created.");
			}

			LogQualificationInfo(std::format(
				"qualify resource: {} create/destroy cycles retired with slot and "
				"descriptor reuse.", kIterations));
			return 0;
		}

		// Runs the minimal-frame qualification script: continuous presents,
		// first-image abort, already-presented abort, normal/abort
		// alternation, continuous abort, resize, minimize/restore and VSync
		// switching. Every step keeps the partial application command buffer
		// unsubmitted; AbortFrame uses the dedicated minimal command buffer.
		[[nodiscard]] int RunVulkanQualificationFrames(
			VulkanFrameRuntime& runtime, HWND hwnd) noexcept
		{
			QualificationFrameStats stats;
			uint32_t step = 0;
			const auto runNormal = [&runtime, hwnd, &stats, &step]()
				{
					return RunQualificationStep(runtime, hwnd, step++, false, stats);
				};
			const auto runAbort = [&runtime, hwnd, &stats, &step]()
				{
					return RunQualificationStep(runtime, hwnd, step++, true, stats);
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
			// Deferred-retirement probe: the gate value is reserved, not
			// submitted, so the slot must stay occupied; the submissions
			// in the remaining phases complete the gate.
			uint32_t deferredPendingSlot = UINT32_MAX;
			if (RunVulkanDeferredRetirementProbe(
				*runtime.GetDevice(), runtime, deferredPendingSlot) != 0)
			{
				return 1;
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
				if (!RunQualificationRecreateChecked(runtime, hwnd, runtime.GetVsync(), stats))
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
			if (!RunQualificationRecreateChecked(runtime, hwnd, true, stats))
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
			if (!RunQualificationRecreateChecked(runtime, hwnd, false, stats))
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
			if (!RunQualificationRecreateChecked(runtime, hwnd, runtime.GetVsync(), stats))
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

			if (runtime.WaitIdle() != VK_SUCCESS)
			{
				LogQualificationError(
					"qualify WaitIdle failed before the final summary; the runtime may have "
					"entered the fatal state.");
				return 1;
			}

			// The submissions after the probe passed the reserved gate
			// value: the pending slot must now be released and reusable.
			if (RunVulkanDeferredRetirementRelease(
				*runtime.GetDevice(), deferredPendingSlot) != 0)
			{
				return 1;
			}

			// Resource layer qualification: buffers, textures, views,
			// samplers, persistent mapping and deferred retirement on the
			// real device.
			if (RunVulkanResourceQualification(*runtime.GetDevice(), runtime) != 0)
			{
				return 1;
			}

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
