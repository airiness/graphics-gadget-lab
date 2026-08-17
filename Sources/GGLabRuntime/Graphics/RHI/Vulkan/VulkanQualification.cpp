#include "Graphics/RHI/Vulkan/VulkanQualification.h"
#include "Core/Log/LogMacros.h"
#include "Graphics/Asset/BuiltinTextureFactory.h"
#include "Graphics/Asset/IBLStageArtifact.h"
#include "Graphics/Asset/Streaming/AssetUploadScheduler.h"
#include "Graphics/Asset/TextureAssetValidation.h"
#include "Graphics/RHI/RHIFormat.h"
#include "Graphics/TransferManager.h"
#if GGLAB_ENABLE_VULKAN
#include "Graphics/RHI/Vulkan/VulkanBarrier.h"
#include "Graphics/RHI/Vulkan/VulkanBootstrap.h"
#include "Graphics/RHI/Vulkan/VulkanCommandContext.h"
#include "Graphics/RHI/Vulkan/VulkanDynamicUniformBuffer.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"
#include "Graphics/RHI/Vulkan/VulkanTransferContext.h"
#include "Graphics/RHI/Vulkan/VulkanWin32Surface.h"
#include "Compiler/ShaderCompiler.h"
#include "Targets/Vulkan13ShaderTarget.h"
#endif

#include <array>
#include <cstring>
#include <cstdint>
#include <string_view>
#include <thread>

namespace gglab
{
	namespace
	{
#if GGLAB_ENABLE_VULKAN
		[[nodiscard]] VulkanBootstrapOptions MakeVulkanBootstrapOptions(
			const VulkanQualificationOptions& options,
			const VulkanSurfaceFactoryBase& surfaceFactory) noexcept
		{
			VulkanBootstrapOptions bootstrapOptions{};
			bootstrapOptions.m_SurfaceFactory = &surfaceFactory;
			bootstrapOptions.m_RequestValidation = options.m_RequestValidation;
			bootstrapOptions.m_SelectionRequest =
				ParseVulkanAdapterSelectionRequest(options.m_AdapterSelector);
			return bootstrapOptions;
		}

		[[nodiscard]] TextureAssetData MakeQualificationTextureData(
			RHIFormat format, RHIExtent3D extent, uint64_t rowPitch,
			std::vector<std::byte> pixels) noexcept
		{
			TextureAssetData result{};
			result.m_ResourceFormat = format;
			result.m_ViewFormat = format;
			result.m_SrvDimension = RHITextureViewDimension::Texture2D;
			result.m_Extent = extent;
			result.m_Pixels = std::move(pixels);
			result.m_Subresources.push_back({
				.m_DataOffset = 0,
				.m_DataSize = result.m_Pixels.size(),
				.m_RowPitch = rowPitch,
				.m_SlicePitch = result.m_Pixels.size(),
				.m_Width = extent.m_Width,
				.m_Height = extent.m_Height,
				.m_Depth = extent.m_Depth,
				.m_MipLevel = 0,
				.m_ArraySlice = 0,
				});
			return result;
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
				GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
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
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
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
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
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
				GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
					"qualify[{:03d}] recreation pending; recreating at the safe point.", step));
				if (!RunQualificationRecreateChecked(runtime, hwnd, runtime.GetVsync(), stats))
				{
					return false;
				}
			}
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
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
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("GetClientRect failed before swapchain recreate.");
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
				GGLAB_LOG_GRAPHICS_INFO_ALWAYS(
					"qualify[recreate] drawable extent is zero; recreation suspended.");
				return VulkanQualificationRecreateOutcome::Suspended;
			}

			std::string error;
			if (!runtime.RecreateSwapChain(width, height, vsync, error))
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format("Swapchain recreate failed: {}", error));
				return VulkanQualificationRecreateOutcome::Failed;
			}
			++stats.m_RecreateCount;
			const auto& swapChain = runtime.GetSwapChain();
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
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
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
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
		struct VulkanDeferredRetirementProbeState
		{
			uint32_t m_TextureSlot = UINT32_MAX;
			uint32_t m_TextureViewSlot = UINT32_MAX;
			uint32_t m_DescriptorIndex = UINT32_MAX;
		};

		[[nodiscard]] int RunVulkanDeferredRetirementProbe(
			VulkanDevice& device, VulkanFrameRuntime& runtime,
			VulkanDeferredRetirementProbeState& outPending) noexcept
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
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify resource: deferred texture creation failed.");
				return 1;
			}
			const RHITextureViewHandle pendingView =
				resources.CreateTextureView(pending, RHITextureViewDesc{});
			const RHIDescriptorHandle pendingDescriptor =
				resources.GetTextureViewDescriptor(pendingView);
			if (!pendingView.IsValid() || !pendingDescriptor.IsValid())
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify resource: deferred texture view creation failed.");
				return 1;
			}
			if (!resources.PublishTextureViewDescriptor(pendingView))
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify resource: deferred descriptor publication failed.");
				return 1;
			}
			resources.RecordTextureUse(pending, incompletePoint);
			// Direct view destruction must join the parent's last-use gate;
			// otherwise the backing view and descriptor index could be reused
			// while a submitted descriptor still references them.
			resources.DestroyTextureView(pendingView);
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
			const RHITextureViewHandle probeView =
				resources.CreateTextureView(probe, RHITextureViewDesc{});
			const RHIDescriptorHandle probeDescriptor =
				resources.GetTextureViewDescriptor(probeView);
			if (!probe.IsValid() || !probeView.IsValid() || !probeDescriptor.IsValid() ||
				probe.Index() == pending.Index() || probeView.Index() == pendingView.Index() ||
				probeDescriptor.m_Index == pendingDescriptor.m_Index)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify resource: incomplete retirement gate released a resource, view or "
					"descriptor slot.");
				return 1;
			}
			resources.DestroyTextureView(probeView);
			resources.DestroyTexture(probe);
			resources.RetireCompletedResources();
			outPending = {
				.m_TextureSlot = pending.Index(),
				.m_TextureViewSlot = pendingView.Index(),
				.m_DescriptorIndex = pendingDescriptor.m_Index,
			};
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
				"qualify resource: incomplete gate at timeline {} retained resource/view/"
				"descriptor slots {}/{}/{}.", incompletePoint.m_Value, pending.Index(),
				pendingView.Index(), pendingDescriptor.m_Index));
			return 0;
		}

		// After the submissions that followed the probe, the gate value has
		// been passed: the pending slot must now be released and reusable.
		[[nodiscard]] int RunVulkanDeferredRetirementRelease(
			VulkanDevice& device, const VulkanDeferredRetirementProbeState& pending) noexcept
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
			const RHITextureViewHandle probeView =
				resources.CreateTextureView(probe, RHITextureViewDesc{});
			const RHIDescriptorHandle probeDescriptor =
				resources.GetTextureViewDescriptor(probeView);
			if (!probe.IsValid() || !probeView.IsValid() || !probeDescriptor.IsValid() ||
				probe.Index() != pending.m_TextureSlot ||
				probeView.Index() != pending.m_TextureViewSlot ||
				probeDescriptor.m_Index != pending.m_DescriptorIndex)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify resource: completed gate did not release the pending resource, "
					"view and descriptor slots together.");
				return 1;
			}
			resources.DestroyTextureView(probeView);
			resources.DestroyTexture(probe);
			resources.RetireCompletedResources();
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
				"qualify resource: deferred gate completed; resource/view/descriptor slots "
				"{}/{}/{} were released and reused together.", pending.m_TextureSlot,
				pending.m_TextureViewSlot, pending.m_DescriptorIndex));
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
				RHIBufferHandle structured = resources.CreateBuffer(
					RHIBufferDesc{ .m_SizeInBytes = 4096,
						.m_StrideInBytes = 16,
						.m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::UnorderedAccess },
					{ .m_Domain = RHIResourceDebugDomain::Diagnostics });
				if (!upload.IsValid() || !readback.IsValid() || !structured.IsValid())
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify resource: buffer creation failed.");
					return 1;
				}
				const RHIBufferViewHandle structuredView = resources.CreateBufferView(structured,
					RHIBufferViewDesc{ .m_Type = RHIBufferViewType::ShaderResource,
						.m_SizeInBytes = 4096, .m_StrideInBytes = 16 });
				const RHIBufferViewHandle typedSrv = resources.CreateBufferView(structured,
					RHIBufferViewDesc{ .m_Type = RHIBufferViewType::ShaderResource,
						.m_SizeInBytes = 4096, .m_Format = RHIFormat::R32Uint });
				const RHIBufferViewHandle typedUav = resources.CreateBufferView(structured,
					RHIBufferViewDesc{ .m_Type = RHIBufferViewType::UnorderedAccess,
						.m_SizeInBytes = 4096, .m_Format = RHIFormat::R32Uint });
				if (!structuredView.IsValid() || !typedSrv.IsValid() || !typedUav.IsValid() ||
					resources.GetBufferViewDescriptor(structuredView).IsValid() ||
					resources.GetBufferViewDescriptor(typedSrv).IsValid() ||
					resources.GetBufferViewDescriptor(typedUav).IsValid())
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
						"qualify resource: fixed-set structured/typed buffer view contract failed.");
					return 1;
				}
				void* mapped = resources.MapBuffer(upload, { 0, 4096 });
				if (mapped == nullptr)
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify resource: upload buffer mapping failed.");
					return 1;
				}
				std::memset(mapped, 0xAB, 4096);
				resources.UnmapBuffer(upload, { 0, 4096 });
				if (resources.MapBuffer(readback, { 0, 4096 }) == nullptr)
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify resource: readback buffer mapping failed.");
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
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify resource: texture creation failed.");
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
				const RHISamplerHandle samplerAlias = resources.CreateSampler(samplerDesc);
				RHISamplerDesc comparisonSamplerDesc{};
				comparisonSamplerDesc.m_Filter =
					RHISamplerFilter::ComparisonMinMagLinearMipPoint;
				comparisonSamplerDesc.m_CompareOp = RHICompareOp::Never;
				const RHISamplerHandle comparisonSampler =
					resources.CreateSampler(comparisonSamplerDesc);
				RHISamplerDesc anisotropicSamplerDesc{};
				anisotropicSamplerDesc.m_Filter = RHISamplerFilter::Anisotropic;
				anisotropicSamplerDesc.m_MaxAnisotropy = 16;
				const RHISamplerHandle anisotropicSampler =
					resources.CreateSampler(anisotropicSamplerDesc);
				if (!srv.IsValid() || !dsv.IsValid() || !unorm.IsValid() ||
					!srgb.IsValid() || !sampler.IsValid() || samplerAlias != sampler ||
					!comparisonSampler.IsValid() ||
					!anisotropicSampler.IsValid())
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify resource: view or sampler creation failed.");
					return 1;
				}

				// Bindless contract: only shader-visible views hold a
				// descriptor index.
				const RHIDescriptorHandle srvDescriptor =
					resources.GetTextureViewDescriptor(srv);
				if (!srvDescriptor.IsValid() ||
					resources.GetTextureViewDescriptor(dsv).IsValid())
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
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
						GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
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
							GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
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
						GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
							"qualify resource: descriptor indices were not reused.");
						return 1;
					}
				}

				resources.RecordTextureUse(color, completedPoint);
				resources.RecordBufferUse(upload, completedPoint);
				resources.RecordBufferUse(structured, completedPoint);
				resources.DestroyTextureView(srv);
				resources.DestroyTextureView(dsv);
				resources.DestroyTextureView(unorm);
				resources.DestroyTextureView(srgb);
				resources.DestroyBufferView(structuredView);
				resources.DestroyBufferView(typedSrv);
				resources.DestroyBufferView(typedUav);
				resources.DestroySampler(sampler);
				if (!resources.IsSamplerAlive(samplerAlias) ||
					!resources.GetSamplerDescriptor(samplerAlias).IsValid())
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
						"qualify resource: cached sampler did not retain shared ownership.");
					return 1;
				}
				resources.DestroySampler(samplerAlias);
				resources.DestroySampler(comparisonSampler);
				resources.DestroySampler(anisotropicSampler);
				resources.DestroyTexture(color);
				resources.DestroyTexture(depth);
				resources.DestroyTexture(typeless);
				resources.DestroyBuffer(upload);
				resources.DestroyBuffer(readback);
				resources.DestroyBuffer(structured);
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
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
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
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
						"qualify resource: typeless UAV family creation failed.");
					return 1;
				}
				resources.DestroyTextureView(srgbSrv);
				resources.DestroyTextureView(unormUav);
				resources.DestroyTexture(typelessUav);
				resources.RetireCompletedResources();
				GGLAB_LOG_GRAPHICS_INFO_ALWAYS(
					"qualify resource: typeless UAV family (SRGB SRV + UNORM UAV) created.");
			}

			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
				"qualify resource: {} create/destroy cycles retired with slot and "
				"descriptor reuse.", kIterations));
			return 0;
		}

		[[nodiscard]] int RunVulkanTransferQualification(
			VulkanDevice& device, VulkanFrameRuntime& runtime) noexcept
		{
			constexpr RHIResourceState commonState{
				.m_Stages = RHIStage::All,
				.m_Access = RHIAccess::Common,
				.m_Layout = RHILayout::Common,
			};
			constexpr RHIResourceState copySourceState{
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopySource,
				.m_Layout = RHILayout::CopySource,
			};
			constexpr RHIResourceState copyDestState{
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopyDest,
				.m_Layout = RHILayout::CopyDest,
			};
			constexpr RHIResourceState shaderResourceState{
				.m_Stages = RHIStage::PixelShader | RHIStage::ComputeShader,
				.m_Access = RHIAccess::ShaderResource,
				.m_Layout = RHILayout::ShaderResource,
			};

			TransferManager transferManager(
				std::make_unique<VulkanTransferContext>(&device));

			// Buffer staging and raw-copy publication exercise explicit terminal
			// states on the shared graphics queue/timeline.
			{
				constexpr std::array<std::byte, 16> payload{
					std::byte{ 0x10 }, std::byte{ 0x21 }, std::byte{ 0x32 }, std::byte{ 0x43 },
					std::byte{ 0x54 }, std::byte{ 0x65 }, std::byte{ 0x76 }, std::byte{ 0x87 },
					std::byte{ 0x98 }, std::byte{ 0xA9 }, std::byte{ 0xBA }, std::byte{ 0xCB },
					std::byte{ 0xDC }, std::byte{ 0xED }, std::byte{ 0xFE }, std::byte{ 0x0F },
				};
				RHIBufferOwner gpuBuffer(&device, device.CreateBuffer({
					.m_SizeInBytes = payload.size(),
					.m_Usage = RHIBufferUsage::CopySource | RHIBufferUsage::CopyDest,
					}, { .m_Domain = RHIResourceDebugDomain::Diagnostics,
						.m_Label = "Qualification.BufferUpload" }));
				RHIBufferOwner readbackBuffer(&device, device.CreateBuffer({
					.m_SizeInBytes = payload.size(),
					.m_Usage = RHIBufferUsage::CopyDest,
					.m_MemoryUsage = RHIMemoryUsage::GpuToCpu,
					}, { .m_Domain = RHIResourceDebugDomain::Diagnostics,
						.m_Label = "Qualification.BufferReadback" }));
				if (!gpuBuffer || !readbackBuffer)
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify transfer: buffer creation failed.");
					return 1;
				}

				TransferBatch uploadBatch = transferManager.BeginBatch();
				const bool uploadRecorded = uploadBatch.UploadBuffer(
					gpuBuffer.Get(), 0, payload.data(), payload.size());
				const RHITransferSubmission uploadSubmission = uploadBatch.Submit(true);
				if (!uploadRecorded || !uploadSubmission.m_Completion.IsValid() ||
					uploadSubmission.m_Publications.size() != 1 ||
					uploadSubmission.m_Publications.front().m_Buffer != gpuBuffer.Get() ||
					uploadSubmission.m_Publications.front().m_PublishedState != commonState ||
					!device.IsFencePointCompleted(uploadSubmission.m_Completion))
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
						"qualify transfer: buffer upload completion/publication contract failed.");
					return 1;
				}

				TransferBatch readbackBatch = transferManager.BeginBatch();
				const std::array barriers{
					RHIBufferBarrier{
						.m_Buffer = gpuBuffer.Get(),
						.m_Before = commonState,
						.m_After = copySourceState,
						},
					RHIBufferBarrier{
						.m_Buffer = readbackBuffer.Get(),
						.m_Before = commonState,
						.m_After = copyDestState,
						},
				};
				readbackBatch.BufferBarrier(barriers);
				readbackBatch.CopyBuffer(
					readbackBuffer.Get(), 0, gpuBuffer.Get(), 0, payload.size());
				const bool sourcePublished =
					readbackBatch.PublishBuffer(gpuBuffer.Get(), copySourceState);
				const bool destinationPublished =
					readbackBatch.PublishBuffer(readbackBuffer.Get(), copyDestState);
				const RHITransferSubmission readbackSubmission = readbackBatch.Submit(true);
				const auto* mapped = static_cast<const std::byte*>(
					device.MapBuffer(readbackBuffer.Get(), { 0, payload.size() }));
				const bool bytesMatch = mapped &&
					std::ranges::equal(payload, std::span(mapped, payload.size()));
				device.UnmapBuffer(readbackBuffer.Get(), {});
				if (!sourcePublished || !destinationPublished ||
					!readbackSubmission.m_Completion.IsValid() ||
					readbackSubmission.m_Publications.size() != 2 || !bytesMatch)
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
						"qualify transfer: explicit buffer copy/readback contract failed.");
					return 1;
				}
			}

			// A decoded texture artifact validates row/slice copies and the
			// Undefined -> CopyDest -> Common upload/readback lifecycle.
			TextureAssetData textureData = MakeQualificationTextureData(
				RHIFormat::R8G8B8A8Unorm, { 2, 2, 1 }, 8,
				{
					std::byte{ 0xFF }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0xFF },
					std::byte{ 0x00 }, std::byte{ 0xFF }, std::byte{ 0x00 }, std::byte{ 0xFF },
					std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0xFF }, std::byte{ 0xFF },
					std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF },
				});
			if (!textureData.IsValid())
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify transfer: texture artifact fixture is invalid.");
				return 1;
			}
			const RHITextureDesc textureDesc{
				.m_Format = textureData.m_ResourceFormat,
				.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::CopySource |
					RHITextureUsage::CopyDest,
				.m_Extent = textureData.m_Extent,
			};
			RHITextureOwner texture(&device, device.CreateTexture({
				.m_Desc = textureDesc,
				.m_InitialState = UndefinedRHITextureState(),
				}, { .m_Domain = RHIResourceDebugDomain::Diagnostics,
					.m_Label = "Qualification.TextureArtifact" }));
			if (!texture)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify transfer: texture artifact creation failed.");
				return 1;
			}
			{
				TransferBatch batch = transferManager.BeginBatch();
				const bool recorded = batch.UploadTexture(texture.Get(), textureData.MakeUploadData(),
					UndefinedRHITextureState(), commonState);
				const RHITransferSubmission submission = batch.Submit(true);
				if (!recorded || !submission.m_Completion.IsValid() ||
					submission.m_Publications.size() != 1 ||
					submission.m_Publications.front().m_Texture != texture.Get() ||
					submission.m_Publications.front().m_PublishedState != commonState)
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
						"qualify transfer: texture artifact publication contract failed.");
					return 1;
				}
			}
			{
				TransferBatch batch = transferManager.BeginBatch();
				RHITextureReadbackRequest request = batch.ReadbackTexture(texture.Get(), textureDesc);
				const RHITransferSubmission submission = batch.Submit(true);
				const std::byte* mapped = transferManager.MapTextureReadback(device, request);
				const TextureAssetData resolved =
					transferManager.ResolveMappedTextureReadback(request, mapped);
				transferManager.UnmapTextureReadback(device, request);
				if (!request.IsValid() || !submission.m_Completion.IsValid() ||
					submission.m_Publications.size() != 2 ||
					resolved.m_Pixels != textureData.m_Pixels)
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
						"qualify transfer: texture artifact readback bytes did not match.");
					return 1;
				}
			}

			// Exercise the actual generated reserved-texture payloads as one synchronous
			// batch, matching the startup path that must finish before material fallback
			// descriptors become available.
			{
				std::vector<BuiltinTextureAsset> bootstrapTextures =
					BuiltinTextureFactory::BuildBootstrapTextures();
				if (bootstrapTextures.empty())
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
						"qualify transfer: bootstrap texture factory returned no textures.");
					return 1;
				}

				struct BootstrapTextureResources
				{
					VulkanDevice& m_Device;
					std::vector<RHITextureOwner> m_Textures;
					std::vector<RHITextureViewHandle> m_Views;

					~BootstrapTextureResources()
					{
						for (const RHITextureViewHandle view : m_Views)
						{
							m_Device.DestroyTextureView(view);
						}
					}
				} resources{ device };
				resources.m_Textures.reserve(bootstrapTextures.size());
				resources.m_Views.reserve(bootstrapTextures.size());

				TransferBatch batch = transferManager.BeginBatch();
				for (const BuiltinTextureAsset& bootstrapTexture : bootstrapTextures)
				{
					if (!ValidateTextureUploadForDevice(bootstrapTexture.m_Data, device).IsValid())
					{
						GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
							"qualify transfer: bootstrap texture '{}' is invalid for the selected device.",
							bootstrapTexture.m_Name));
						return 1;
					}

					const RHITextureDesc desc = BuildTextureRHITextureDesc(bootstrapTexture.m_Data);
					resources.m_Textures.emplace_back(&device, device.CreateTexture({
						.m_Desc = desc,
						.m_InitialState = UndefinedRHITextureState(),
						}, {
							.m_Domain = RHIResourceDebugDomain::Diagnostics,
							.m_Category = "BootstrapTexture",
							.m_Label = bootstrapTexture.m_Name,
						}));
						const RHITextureHandle texture = resources.m_Textures.back().Get();
						if (!texture.IsValid())
						{
							GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
								"qualify transfer: bootstrap texture '{}' creation failed.",
								bootstrapTexture.m_Name));
							return 1;
						}

						const RHITextureViewHandle view = device.CreateTextureView(
							texture, BuildTextureRHISRVDesc(bootstrapTexture.m_Data));
						if (view.IsValid())
						{
							resources.m_Views.push_back(view);
						}
						if (!view.IsValid() || !device.GetTextureViewDescriptor(view).IsValid())
						{
							GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
								"qualify transfer: bootstrap texture '{}' SRV creation failed.",
								bootstrapTexture.m_Name));
							return 1;
						}
						if (!batch.UploadTexture(texture, bootstrapTexture.m_Data.MakeUploadData(),
							UndefinedRHITextureState(), shaderResourceState))
						{
							GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
								"qualify transfer: bootstrap texture '{}' upload recording failed.",
								bootstrapTexture.m_Name));
							return 1;
						}
				}

				const RHITransferSubmission submission = batch.Submit(true);
				const bool everyTexturePublished = submission.m_Completion.IsValid() &&
					device.IsFencePointCompleted(submission.m_Completion) &&
					submission.m_Publications.size() == resources.m_Textures.size() &&
					std::ranges::all_of(resources.m_Textures,
						[&](const RHITextureOwner& textureOwner) noexcept
						{
							return std::ranges::count_if(submission.m_Publications,
								[&](const RHITransferResourcePublication& publication) noexcept
								{
									return publication.m_Type == RHIResourceType::Texture &&
										publication.m_Texture == textureOwner.Get() &&
										publication.m_PublishedState == shaderResourceState;
								}) == 1;
						});
				if (!everyTexturePublished)
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
						"qualify transfer: bootstrap texture batch publication failed.");
					return 1;
				}
				const bool everyDescriptorPublished = std::ranges::all_of(resources.m_Views,
					[&device](RHITextureViewHandle view) noexcept
					{
						return device.GetResourceManager().PublishTextureViewDescriptor(view);
					});
				if (!everyDescriptorPublished)
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
						"qualify transfer: bootstrap descriptor publication failed.");
					return 1;
				}
				const VulkanDescriptorPublicationDiagnostics descriptorDiagnostics =
					device.GetDescriptorManager().GetResourceDiagnostics();
				if (descriptorDiagnostics.m_LiveCount < resources.m_Views.size() ||
					descriptorDiagnostics.m_RetainedBackingCount < resources.m_Views.size() ||
					descriptorDiagnostics.m_EstimatedRetainedBytes == 0)
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
						"qualify transfer: bootstrap descriptor backing diagnostics are incomplete.");
					return 1;
				}
				GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
					"qualify transfer: {} bootstrap textures uploaded and published "
					"(retained backing estimate={} bytes).",
					resources.m_Textures.size(),
					descriptorDiagnostics.m_EstimatedRetainedBytes));
			}

			// Use a real IBL stage artifact, not a synthetic RHI-only payload,
			// to keep the cache/streaming ABI in the qualification path.
			IBLStageArtifactHandle iblArtifact = CreateIBLStageArtifact(
				IBLArtifactStage::BrdfLut,
				MakeQualificationTextureData(RHIFormat::R16G16Float, { 2, 2, 1 }, 8,
					std::vector<std::byte>(16, std::byte{ 0x3C })));
			if (!iblArtifact)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify transfer: IBL stage artifact fixture is invalid.");
				return 1;
			}
			const RHITextureDesc iblDesc{
				.m_Format = iblArtifact->m_Texture.m_ResourceFormat,
				.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::CopyDest,
				.m_Extent = iblArtifact->m_Texture.m_Extent,
			};
			RHITextureOwner iblTexture(&device, device.CreateTexture({
				.m_Desc = iblDesc,
				.m_InitialState = UndefinedRHITextureState(),
				}, { .m_Domain = RHIResourceDebugDomain::Diagnostics,
					.m_Label = "Qualification.IBL.BrdfLut" }));
			{
				TransferBatch batch = transferManager.BeginBatch();
				const bool recorded = iblTexture && batch.UploadTexture(iblTexture.Get(),
					iblArtifact->m_Texture.MakeUploadData(), UndefinedRHITextureState(),
					shaderResourceState);
				const RHITransferSubmission submission = batch.Submit(true);
				if (!recorded || !submission.m_Completion.IsValid() ||
					submission.m_Publications.size() != 1 ||
					submission.m_Publications.front().m_PublishedState != shaderResourceState)
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
						"qualify transfer: IBL artifact upload/publication failed.");
					return 1;
				}
			}

			// Workers only enqueue immutable payloads. Resource creation, upload
			// recording, queue submission, completion polling and descriptor
			// publication all execute on the captured graphics owner thread.
			AssetUploadScheduler scheduler({
				.m_Device = &device,
				.m_TransferManager = &transferManager,
				});
			const AssetStreamingIdentity streamingIdentity{
				.m_Kind = AssetStreamingWorkKind::Texture,
				.m_StableId = 8,
				.m_Generation = 1,
			};
			// Hold completion so the test observes the unpublished interval
			// deterministically instead of racing a fast graphics queue.
			scheduler.ArmGpuCompletionHold(streamingIdentity);
			const auto streamingPayload = std::make_shared<const TextureAssetData>(textureData);
			RHITextureOwner streamingTexture;
			RHITextureViewHandle streamingView;
			RHIDescriptorHandle privateDescriptor;
			RHIDescriptorHandle publishedDescriptor;
			bool workerSawOwner = true;
			bool offOwnerMutationRejected = false;
			bool resourceWorkRanOnOwner = false;
			bool uploadWorkRanOnOwner = false;
			bool completionRanOnOwner = false;
			bool uploadHandleValid = false;
			std::thread worker([&]()
				{
					workerSawOwner = scheduler.IsOwnerThread();
					offOwnerMutationRejected = !device.CreateBuffer({
						.m_SizeInBytes = 16,
						.m_Usage = RHIBufferUsage::CopyDest,
						}, { .m_Domain = RHIResourceDebugDomain::Diagnostics,
							.m_Label = "Qualification.ExpectedOffOwnerRejection" }).IsValid();
					scheduler.EnqueueCpuPayload({
						.m_Name = "Vulkan texture payload handoff",
						.m_Identity = streamingIdentity,
						.m_Estimate = {
							.m_SourceBytes = streamingPayload->m_Pixels.size(),
							},
						},
						[&]()
						{
							resourceWorkRanOnOwner = scheduler.IsOwnerThread();
							streamingTexture = RHITextureOwner(&device, device.CreateTexture({
								.m_Desc = textureDesc,
								.m_InitialState = UndefinedRHITextureState(),
								}, { .m_Domain = RHIResourceDebugDomain::Asset,
									.m_Label = "Qualification.StreamedTexture" }));
							if (!streamingTexture)
							{
								return;
							}
							streamingView = device.CreateTextureView(streamingTexture.Get(), {
								.m_Type = RHITextureViewType::ShaderResource,
								.m_Dimension = streamingPayload->m_SrvDimension,
								.m_Format = streamingPayload->m_ViewFormat,
								});
							privateDescriptor = device.GetTextureViewDescriptor(streamingView);
							if (!streamingView.IsValid() || !privateDescriptor.IsValid())
							{
								return;
							}
							scheduler.EnqueueUploadRecording({
								.m_Name = "Vulkan streamed texture upload",
								.m_Identity = streamingIdentity,
								.m_Estimate = {
									.m_SourceBytes = streamingPayload->m_Pixels.size(),
									.m_StagingBytes = streamingPayload->m_Pixels.size(),
									.m_OperationCount = 1,
									},
								},
								[&]()
								{
									uploadWorkRanOnOwner = scheduler.IsOwnerThread();
									const AssetUploadHandle handle = scheduler.RecordUpload({
										.m_Name = "Vulkan streamed texture upload",
										.m_Identity = streamingIdentity,
										.m_Estimate = {
											.m_StagingBytes = streamingPayload->m_Pixels.size(),
											.m_OperationCount = 1,
											},
										},
										[&](TransferBatch& batch)
										{
											return batch.UploadTexture(streamingTexture.Get(),
												streamingPayload->MakeUploadData(),
												UndefinedRHITextureState(), shaderResourceState);
										},
										[&](const AssetUploadCompletionInfo& completion)
										{
											completionRanOnOwner = scheduler.IsOwnerThread();
											if (completion.m_Status == AssetUploadStatus::Succeeded &&
												completion.m_FencePoint.IsValid() &&
												device.IsFencePointCompleted(completion.m_FencePoint) &&
												device.GetResourceManager().PublishTextureViewDescriptor(
													streamingView))
											{
												publishedDescriptor = privateDescriptor;
											}
										});
									uploadHandleValid = handle.IsValid();
								});
						});
				});
			worker.join();
			scheduler.DrainReadyWork();
			const bool publicationWithheld = privateDescriptor.IsValid() &&
				!publishedDescriptor.IsValid();
			if (runtime.WaitIdle() != VK_SUCCESS)
			{
				scheduler.ClearGpuCompletionHold();
				scheduler.Finalize();
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify transfer: WaitIdle failed after scheduler upload.");
				return 1;
			}
			scheduler.ClearGpuCompletionHold();
			GGLAB_UNUSED(scheduler.Tick());
			const AssetUploadStatistics statistics = scheduler.GetStatistics();
			const bool descriptorPublished = publishedDescriptor.IsValid() &&
				publishedDescriptor.m_HeapType == privateDescriptor.m_HeapType &&
				publishedDescriptor.m_Index == privateDescriptor.m_Index;
			const bool schedulerPassed = !workerSawOwner && offOwnerMutationRejected &&
				resourceWorkRanOnOwner && uploadWorkRanOnOwner && completionRanOnOwner &&
				uploadHandleValid && publicationWithheld && descriptorPublished &&
				statistics.m_PendingCount == 0 && statistics.m_SubmittedCount == 1 &&
				statistics.m_SucceededCount == 1;
			if (statistics.m_PendingCount == 0)
			{
				scheduler.Finalize();
			}
			if (streamingView.IsValid())
			{
				device.DestroyTextureView(streamingView);
			}
			streamingTexture.Reset();
			iblTexture.Reset();
			texture.Reset();
			transferManager.Reclaim();
			if (!schedulerPassed)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify transfer: worker handoff or completion-gated publication failed.");
				return 1;
			}

			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(
				"qualify transfer: buffer, texture artifact, bootstrap texture, IBL artifact and owner-thread scheduler paths passed.");
			return 0;
		}

		[[nodiscard]] int RunVulkanDescriptorPipelineQualification(
			VulkanDevice& device, VulkanFrameRuntime& runtime,
			const std::filesystem::path& shaderSourceRoot,
			const std::filesystem::path& shaderCacheRoot) noexcept
		{
			VulkanDescriptorManager& descriptors = device.GetDescriptorManager();
			if (!descriptors.IsLayoutSupported() ||
				descriptors.GetGlobalSetLayout() == VK_NULL_HANDLE ||
				descriptors.GetGlobalSet() == VK_NULL_HANDLE)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify descriptors: the exact global descriptor layout is unavailable.");
				return 1;
			}

			VulkanResourceManager& resources = device.GetResourceManager();
			RHISamplerDesc publicationSamplerDesc{};
			const RHISamplerHandle submittedSampler =
				resources.CreateSampler(publicationSamplerDesc);
			const RHIDescriptorHandle submittedSamplerDescriptor =
				resources.GetSamplerDescriptor(submittedSampler);
			const bool submittedSamplerPublished = submittedSampler.IsValid() &&
				submittedSamplerDescriptor.IsValid() &&
				resources.PublishSamplerDescriptor(submittedSampler);
			if (!submittedSamplerPublished)
			{
				if (submittedSampler.IsValid())
				{
					resources.DestroySampler(submittedSampler);
				}
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify descriptors: submitted sampler publication setup failed.");
				return 1;
			}
			const VulkanBeginFrameResult submittedBegin = runtime.BeginFrame();
			if (!submittedBegin.IsAcquired())
			{
				resources.DestroySampler(submittedSampler);
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify descriptors: submitted sampler frame acquire failed.");
				return 1;
			}
			resources.DestroySampler(submittedSampler);
			const VulkanDescriptorPublicationDiagnostics pendingSubmittedDiagnostics =
				descriptors.GetSamplerDiagnostics();
			const VulkanDescriptorPublicationState pendingSubmittedState =
				descriptors.GetSamplerState(submittedSamplerDescriptor.m_Index);
			const VulkanSubmitPresentResult submittedFrame =
				runtime.EndFrame({ 0.08f, 0.12f, 0.18f, 1.0f });
			if (pendingSubmittedState != VulkanDescriptorPublicationState::Live ||
				pendingSubmittedDiagnostics.m_RetirementRequestedCount == 0 ||
				!submittedFrame.m_Submitted || runtime.WaitIdle() != VK_SUCCESS)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify descriptors: submitted sampler retirement was not fence-gated.");
				return 1;
			}
			device.RetireCompletedWork();
			if (descriptors.GetSamplerState(submittedSamplerDescriptor.m_Index) !=
				VulkanDescriptorPublicationState::Free)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify descriptors: submitted sampler retirement did not complete.");
				return 1;
			}

			const RHISamplerHandle abandonedSampler =
				resources.CreateSampler(publicationSamplerDesc);
			const RHIDescriptorHandle abandonedSamplerDescriptor =
				resources.GetSamplerDescriptor(abandonedSampler);
			const bool abandonedSamplerPublished = abandonedSampler.IsValid() &&
				abandonedSamplerDescriptor.IsValid() &&
				resources.PublishSamplerDescriptor(abandonedSampler);
			if (!abandonedSamplerPublished)
			{
				if (abandonedSampler.IsValid())
				{
					resources.DestroySampler(abandonedSampler);
				}
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify descriptors: abandoned sampler publication setup failed.");
				return 1;
			}
			const VulkanBeginFrameResult abandonedBegin = runtime.BeginFrame();
			if (!abandonedBegin.IsAcquired())
			{
				resources.DestroySampler(abandonedSampler);
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify descriptors: abandoned sampler frame acquire failed.");
				return 1;
			}
			resources.DestroySampler(abandonedSampler);
			const VulkanSubmitPresentResult abandonedFrame = runtime.AbortFrame();
			device.RetireCompletedWork();
			if (!abandonedFrame.m_Submitted ||
				descriptors.GetSamplerState(abandonedSamplerDescriptor.m_Index) !=
				VulkanDescriptorPublicationState::Free)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify descriptors: descriptor-unused abort retained a sampler snapshot.");
				return 1;
			}

			RHIBindingLayoutDesc layoutDesc{};
			layoutDesc.m_DebugName = "Qualification.DynamicUniformLayout";
			layoutDesc.m_Slots[layoutDesc.m_SlotCount++] = {
				.m_Type = RHIBindingType::PushConstants,
				.m_Visibility = RHIShaderStage::All,
				.m_Binding = 2,
				.m_SizeInBytes = 64,
				.m_DebugName = "PassConstants",
			};
			layoutDesc.m_Slots[layoutDesc.m_SlotCount++] = {
				.m_Type = RHIBindingType::PushConstants,
				.m_Visibility = RHIShaderStage::All,
				.m_Binding = 1,
				.m_SizeInBytes = 16,
				.m_DebugName = "DrawConstants",
			};
			layoutDesc.m_Slots[layoutDesc.m_SlotCount++] = {
				.m_Type = RHIBindingType::BindlessResourceTable,
				.m_Visibility = RHIShaderStage::All,
				.m_Count = 0,
				.m_DebugName = "BindlessResources",
			};
			layoutDesc.m_Slots[layoutDesc.m_SlotCount++] = {
				.m_Type = RHIBindingType::BindlessSamplerTable,
				.m_Visibility = RHIShaderStage::All,
				.m_Count = 0,
				.m_DebugName = "BindlessSamplers",
			};

			VulkanPipelineSystem pipelineSystem(&device);
			const RHIBindingLayoutHandle layoutHandle =
				pipelineSystem.CreateBindingLayout(layoutDesc);
			VulkanBindingLayout* layout = pipelineSystem.ResolveBindingLayout(layoutHandle);
			if (!layout || layout->GetPipelineLayout() == VK_NULL_HANDLE ||
				layout->GetPlan().GetDynamicOffsetSlot(1) != 0 ||
				layout->GetPlan().GetDynamicOffsetSlot(0) != 1)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify pipeline: set-0 or pipeline-layout creation failed.");
				return 1;
			}

			const uint32_t uniformAlignment = static_cast<uint32_t>(std::max<VkDeviceSize>(
				device.GetPhysicalDeviceLimits().minUniformBufferOffsetAlignment, 1));
			VulkanDynamicUniformBuffer uniformBuffer;
			if (!uniformBuffer.Initialize(&device, runtime.GetFrameSlotCount(), {
				.m_PageSizeInBytes = uniformAlignment * 2,
				.m_MaxPageCount = 2,
				.m_Alignment = 1,
				}))
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify dynamic uniform: buffer initialization failed.");
				return 1;
			}
			VulkanSet0DynamicUniformFrames set0Frames;
			const bool set0Initialized = set0Frames.Initialize(
				&device, &uniformBuffer, runtime.GetFrameSlotCount());
			if (!set0Initialized || !set0Frames.BeginFrame(0) ||
				set0Frames.BeginFrame(0) || uniformBuffer.GetAlignmentInBytes() != uniformAlignment)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify dynamic uniform: set-0 frame-slot gating failed.");
				return 1;
			}

			VulkanDynamicUniformState uniformState;
			const std::array<uint32_t, 2> firstValues{ 3, 5 };
			const std::array<uint32_t, 1> secondValues{ 8 };
			const bool stateInitialized = uniformState.Initialize(layout->GetPlan());
			const VulkanDynamicUniformUpdate first = uniformState.SetPushConstants(
				1, firstValues, 0, uniformBuffer, 0);
			const VulkanDynamicUniformUpdate second = uniformState.SetPushConstants(
				1, secondValues, 1, uniformBuffer, 0);
			VulkanDynamicUniformUpdate overflow = second;
			for (uint32_t allocationIndex = 0; allocationIndex < 16 && overflow.IsValid();
				++allocationIndex)
			{
				overflow = uniformState.SetPushConstants(
					1, secondValues, 1, uniformBuffer, 0);
			}
			const VulkanDynamicUniformDiagnostics* diagnostics = uniformBuffer.GetDiagnostics(0);
			if (!stateInitialized || !first.IsValid() || !second.IsValid() ||
				first.m_Allocation.m_DynamicOffset == second.m_Allocation.m_DynamicOffset ||
				first.m_Allocation.m_DynamicOffset % uniformAlignment != 0 ||
				second.m_Allocation.m_DynamicOffset % uniformAlignment != 0 || overflow.IsValid() ||
				!diagnostics || diagnostics->m_OverflowCount == 0 ||
				set0Frames.AllocateDescriptorSet(0, *layout, {}) == VK_NULL_HANDLE)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify dynamic uniform: immutable allocation, alignment or overflow failed.");
				return 1;
			}

			const RHIFencePoint completedFrame(
				runtime.GetTimeline().GetRHIHandle(), runtime.GetTimelineSignalValue());
			if (!completedFrame.IsValid() || !set0Frames.EndFrame(0, completedFrame) ||
				!set0Frames.BeginFrame(0) || !set0Frames.EndFrame(0, completedFrame) ||
				!set0Frames.BeginFrame(0) || !set0Frames.AbortFrame(0))
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify dynamic uniform: completed frame-slot reuse failed.");
				return 1;
			}

			ShaderCompiler compiler(shaderSourceRoot, shaderCacheRoot);
			ShaderDesc shaderDesc{
				.m_SourcePath = L"Passes/PassFinalColor.hlsl",
				.m_Stage = ShaderStage::Vertex,
				.m_Target = MakeVulkan13CompileTarget(ShaderStage::Vertex),
				.m_Entry = L"VSMain",
				.m_IncludeDirs = {L"."},
			};
			const ShaderCompileResult compileResult = compiler.Compile(shaderDesc);
			if (!compileResult.IsSuccess())
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify descriptors: shader compile failed.");
				return 1;
			}
			const ShaderBytecode spirV{
				.m_Data = compileResult.m_Artifact.m_Binary.Data(),
				.m_SizeInBytes = compileResult.m_Artifact.m_Binary.SizeInBytes(),
				.m_Format = compileResult.m_Artifact.GetBinaryFormat(),
				.m_Hash = ComputeShaderBinaryHash(compileResult.m_Artifact.m_Binary,
					compileResult.m_Artifact.GetBinaryFormat()),
				.m_EntryPoint = L"VSMain",
			};
			const VkShaderModule shaderModule =
				pipelineSystem.CreateShaderModule(spirV, "Qualification.FinalColorVS");
			ShaderBytecode rejectedBytecode = spirV;
			rejectedBytecode.m_Format = ShaderBinaryFormat::Dxil;
			const bool rejectedDxil =
				pipelineSystem.CreateShaderModule(rejectedBytecode) == VK_NULL_HANDLE;
			if (shaderModule == VK_NULL_HANDLE || !rejectedDxil)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify pipeline: SPIR-V module creation or DXIL rejection failed.");
				return 1;
			}
			pipelineSystem.DestroyShaderModule(shaderModule);
			const uint32_t highWater = diagnostics->m_HighWaterMarkInBytes;
			const uint64_t overflowCount = diagnostics->m_OverflowCount;
			set0Frames.Finalize();
			uniformBuffer.Finalize();
			device.RetireCompletedWork();
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
				"qualify descriptors: global layout, pipeline layout, shader module and dynamic "
				"uniform storage passed (alignment={}, highWater={}, overflow={}).",
				uniformAlignment, highWater, overflowCount));
			return 0;
		}

		[[nodiscard]] int RunVulkanGraphicsQualification(
			VulkanDevice& device, VulkanFrameRuntime& runtime,
			const std::filesystem::path& shaderSourceRoot,
			const std::filesystem::path& shaderCacheRoot) noexcept
		{
			constexpr uint32_t targetWidth = 64;
			constexpr uint32_t targetHeight = 64;
			constexpr RHIFormat colorFormat = RHIFormat::R8G8B8A8Unorm;
			constexpr RHIFormat depthFormat = RHIFormat::D32Float;
			constexpr RHIResourceState shaderResourceState{
				.m_Stages = RHIStage::PixelShader,
				.m_Access = RHIAccess::ShaderResource,
				.m_Layout = RHILayout::ShaderResource,
			};
			enum class CoordinateConformanceMode : uint32_t
			{
				Winding = 0,
				MarkerSampling = 1,
				DepthVisualization = 2,
				Position = 3,
				DepthProbe = 4,
			};
			constexpr float DepthOnlyExpectedDepth = 0.25f;
			constexpr float DepthOnlyVertexDepth = 0.75f;
			constexpr float ReversedZFarProbeDepth = 0.25f;
			constexpr float ReversedZNearProbeDepth = 0.75f;

			struct CoordinateConformanceParameters
			{
				uint32_t m_TextureIndex = 0;
				uint32_t m_SamplerIndex = 0;
				CoordinateConformanceMode m_Mode = CoordinateConformanceMode::Winding;
				float m_Depth = 0.0f;
				float m_TargetExtent[2] = { 1.0f, 1.0f };
				float m_DepthOverride = 0.0f;
				float m_Padding = 0.0f;
			};
			static_assert(sizeof(CoordinateConformanceParameters) == 32);
			struct CoordinateVertex
			{
				float m_Position[3];
				float m_UV[2];
			};
			static_assert(sizeof(CoordinateVertex) == 20);

			VulkanResourceManager& resources = device.GetResourceManager();
			VulkanPipelineSystem pipelineSystem(&device);
			RHIBindingLayoutDesc layoutDesc{};
			layoutDesc.m_DebugName = "Qualification.GraphicsLayout";
			layoutDesc.m_Slots[layoutDesc.m_SlotCount++] = {
				.m_Type = RHIBindingType::PushConstants,
				.m_Visibility = RHIShaderStage::All,
				.m_Binding = 2,
				.m_SizeInBytes = sizeof(CoordinateConformanceParameters),
				.m_DebugName = "PassConstants",
			};
			layoutDesc.m_Slots[layoutDesc.m_SlotCount++] = {
				.m_Type = RHIBindingType::ReadOnlyStorageBuffer,
				.m_Visibility = RHIShaderStage::Compute,
				.m_Binding = 1,
				.m_DebugName = "ComputeInput",
			};
			layoutDesc.m_Slots[layoutDesc.m_SlotCount++] = {
				.m_Type = RHIBindingType::ReadWriteStorageBuffer,
				.m_Visibility = RHIShaderStage::Compute,
				.m_Binding = 1,
				.m_DebugName = "ComputeOutput",
			};
			layoutDesc.m_Slots[layoutDesc.m_SlotCount++] = {
				.m_Type = RHIBindingType::BindlessResourceTable,
				.m_Visibility = RHIShaderStage::All,
				.m_Count = 0,
				.m_DebugName = "BindlessResources",
			};
			layoutDesc.m_Slots[layoutDesc.m_SlotCount++] = {
				.m_Type = RHIBindingType::BindlessSamplerTable,
				.m_Visibility = RHIShaderStage::All,
				.m_Count = 0,
				.m_DebugName = "BindlessSamplers",
			};
			const RHIBindingLayoutHandle layoutHandle =
				pipelineSystem.CreateBindingLayout(layoutDesc);
			VulkanBindingLayout* layout = pipelineSystem.ResolveBindingLayout(layoutHandle);
			if (layout == nullptr || layout->GetPlan().m_DynamicOffsetCount != 1 ||
				layout->GetPlan().GetDynamicOffsetSlot(0) != 0)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: binding layout creation failed.");
				return 1;
			}

			ShaderCompiler compiler(shaderSourceRoot, shaderCacheRoot);
			const auto compileShader = [&compiler](ShaderStage stage, const wchar_t* entry)
				{
					ShaderDesc desc{
						.m_SourcePath = L"Passes/PassCoordinateConformance.hlsl",
						.m_Stage = stage,
						.m_Target = MakeVulkan13CompileTarget(stage),
						.m_Entry = entry,
						.m_IncludeDirs = { L"." },
					};
					return compiler.Compile(desc);
				};
			const ShaderCompileResult geometryResult =
				compileShader(ShaderStage::Vertex, L"VSGeometry");
			const ShaderCompileResult fullscreenResult =
				compileShader(ShaderStage::Vertex, L"VSFullscreen");
			const ShaderCompileResult pixelResult =
				compileShader(ShaderStage::Pixel, L"PSConformance");
			const ShaderCompileResult depthOverrideResult =
				compileShader(ShaderStage::Pixel, L"PSDepthOverride");
			const ShaderCompileResult computeResult =
				compileShader(ShaderStage::Compute, L"CSStorageDependency");
			if (!geometryResult.IsSuccess() || !fullscreenResult.IsSuccess() ||
				!pixelResult.IsSuccess() || !depthOverrideResult.IsSuccess() ||
				!computeResult.IsSuccess() ||
				geometryResult.m_Artifact.GetBinaryFormat() != ShaderBinaryFormat::SpirV ||
				fullscreenResult.m_Artifact.GetBinaryFormat() != ShaderBinaryFormat::SpirV ||
				pixelResult.m_Artifact.GetBinaryFormat() != ShaderBinaryFormat::SpirV ||
				depthOverrideResult.m_Artifact.GetBinaryFormat() != ShaderBinaryFormat::SpirV ||
				computeResult.m_Artifact.GetBinaryFormat() != ShaderBinaryFormat::SpirV)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: coordinate shader compilation failed.");
				return 1;
			}
			const ShaderBytecode geometryShader{
				.m_Data = geometryResult.m_Artifact.m_Binary.Data(),
				.m_SizeInBytes = geometryResult.m_Artifact.m_Binary.SizeInBytes(),
				.m_Format = geometryResult.m_Artifact.GetBinaryFormat(),
				.m_Hash = ComputeShaderBinaryHash(geometryResult.m_Artifact.m_Binary,
					geometryResult.m_Artifact.GetBinaryFormat()),
				.m_EntryPoint = L"VSGeometry",
			};
			const ShaderBytecode fullscreenShader{
				.m_Data = fullscreenResult.m_Artifact.m_Binary.Data(),
				.m_SizeInBytes = fullscreenResult.m_Artifact.m_Binary.SizeInBytes(),
				.m_Format = fullscreenResult.m_Artifact.GetBinaryFormat(),
				.m_Hash = ComputeShaderBinaryHash(fullscreenResult.m_Artifact.m_Binary,
					fullscreenResult.m_Artifact.GetBinaryFormat()),
				.m_EntryPoint = L"VSFullscreen",
			};
			const ShaderBytecode pixelShader{
				.m_Data = pixelResult.m_Artifact.m_Binary.Data(),
				.m_SizeInBytes = pixelResult.m_Artifact.m_Binary.SizeInBytes(),
				.m_Format = pixelResult.m_Artifact.GetBinaryFormat(),
				.m_Hash = ComputeShaderBinaryHash(pixelResult.m_Artifact.m_Binary,
					pixelResult.m_Artifact.GetBinaryFormat()),
				.m_EntryPoint = L"PSConformance",
			};
			const ShaderBytecode depthOverrideShader{
				.m_Data = depthOverrideResult.m_Artifact.m_Binary.Data(),
				.m_SizeInBytes = depthOverrideResult.m_Artifact.m_Binary.SizeInBytes(),
				.m_Format = depthOverrideResult.m_Artifact.GetBinaryFormat(),
				.m_Hash = ComputeShaderBinaryHash(depthOverrideResult.m_Artifact.m_Binary,
					depthOverrideResult.m_Artifact.GetBinaryFormat()),
				.m_EntryPoint = L"PSDepthOverride",
			};
			const ShaderBytecode computeShader{
				.m_Data = computeResult.m_Artifact.m_Binary.Data(),
				.m_SizeInBytes = computeResult.m_Artifact.m_Binary.SizeInBytes(),
				.m_Format = computeResult.m_Artifact.GetBinaryFormat(),
				.m_Hash = ComputeShaderBinaryHash(computeResult.m_Artifact.m_Binary,
					computeResult.m_Artifact.GetBinaryFormat()),
				.m_EntryPoint = L"CSStorageDependency",
			};

			RHIGraphicsPipelineDesc geometryDesc{};
			geometryDesc.m_BindingLayout = layoutHandle;
			geometryDesc.m_VertexInput.m_VertexBuffers[0] = {
				.m_InputSlot = 0,
				.m_StrideInBytes = sizeof(CoordinateVertex),
			};
			geometryDesc.m_VertexInput.m_VertexBufferCount = 1;
			geometryDesc.m_VertexInput.m_Attributes[0] = {
				.m_SemanticName = "POSITION",
				.m_Location = 0,
				.m_Format = RHIFormat::R32G32B32Float,
				.m_InputSlot = 0,
				.m_AlignedByteOffset = 0,
			};
			geometryDesc.m_VertexInput.m_Attributes[1] = {
				.m_SemanticName = "TEXCOORD",
				.m_Location = 1,
				.m_Format = RHIFormat::R32G32Float,
				.m_InputSlot = 0,
				.m_AlignedByteOffset = 12,
			};
			geometryDesc.m_VertexInput.m_AttributeCount = 2;
			geometryDesc.m_Rasterizer.m_CullMode = RHICullMode::None;
			geometryDesc.m_DepthStencil = {
				.m_DepthTestEnable = false,
				.m_DepthWriteEnable = false,
				.m_DepthCompareOp = RHICompareOp::Always,
			};
			geometryDesc.m_RenderTargetFormats[0] = colorFormat;
			geometryDesc.m_RenderTargetCount = 1;
			geometryDesc.m_DepthStencilFormat = depthFormat;

			RHIGraphicsPipelineDesc depthDesc = geometryDesc;
			depthDesc.m_VertexInput = {};
			depthDesc.m_DepthStencil = {
				.m_DepthTestEnable = true,
				.m_DepthWriteEnable = true,
				.m_DepthCompareOp = RHICompareOp::Greater,
			};
			RHIGraphicsPipelineDesc positionDesc = depthDesc;
			positionDesc.m_DepthStencil = geometryDesc.m_DepthStencil;
			RHIGraphicsPipelineDesc backCullDesc = geometryDesc;
			backCullDesc.m_Rasterizer.m_CullMode = RHICullMode::Back;
			RHIGraphicsPipelineDesc depthOnlyDesc = depthDesc;
			depthOnlyDesc.m_RenderTargetFormats = {};
			depthOnlyDesc.m_RenderTargetCount = 0;
			const RHIPipelineHandle geometryPipeline = pipelineSystem.CreateGraphicsPipeline({
				.m_Desc = geometryDesc,
				.m_VertexShader = geometryShader,
				.m_PixelShader = pixelShader,
				});
			const RHIPipelineHandle depthPipeline = pipelineSystem.CreateGraphicsPipeline({
				.m_Desc = depthDesc,
				.m_VertexShader = fullscreenShader,
				.m_PixelShader = pixelShader,
				});
			const RHIPipelineHandle positionPipeline = pipelineSystem.CreateGraphicsPipeline({
				.m_Desc = positionDesc,
				.m_VertexShader = fullscreenShader,
				.m_PixelShader = pixelShader,
				});
			const RHIPipelineHandle backCullPipeline = pipelineSystem.CreateGraphicsPipeline({
				.m_Desc = backCullDesc,
				.m_VertexShader = geometryShader,
				.m_PixelShader = pixelShader,
				});
			const RHIPipelineHandle depthWithoutPixelPipeline =
				pipelineSystem.CreateGraphicsPipeline({
					.m_Desc = depthOnlyDesc,
					.m_VertexShader = fullscreenShader,
					});
			const RHIPipelineHandle depthOverridePipeline = pipelineSystem.CreateGraphicsPipeline({
				.m_Desc = depthOnlyDesc,
				.m_VertexShader = fullscreenShader,
				.m_PixelShader = depthOverrideShader,
				});
			const RHIPipelineHandle computePipeline = pipelineSystem.CreateComputePipeline({
				.m_Desc = {.m_BindingLayout = layoutHandle },
				.m_ComputeShader = computeShader,
				});
			if (!geometryPipeline.IsValid() || !depthPipeline.IsValid() ||
				!positionPipeline.IsValid() || !backCullPipeline.IsValid() ||
				!depthWithoutPixelPipeline.IsValid() || !depthOverridePipeline.IsValid() ||
				!computePipeline.IsValid())
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: native pipeline creation failed.");
				return 1;
			}

			const RHITextureDesc colorDesc{
				.m_Format = colorFormat,
				.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::CopySource,
				.m_Extent = { targetWidth, targetHeight, 1 },
				.m_DebugName = "Qualification.GraphicsColor",
			};
			const RHITextureDesc depthTextureDesc{
				.m_Format = depthFormat,
				.m_Usage = RHITextureUsage::DepthStencil,
				.m_Extent = { targetWidth, targetHeight, 1 },
				.m_DebugName = "Qualification.GraphicsDepth",
			};
			const RHITextureDesc depthWriteProbeDesc{
				.m_Format = depthFormat,
				.m_Usage = RHITextureUsage::DepthStencil | RHITextureUsage::Sampled,
				.m_Extent = { targetWidth, targetHeight, 1 },
				.m_DebugName = "Qualification.GraphicsDepthWriteProbe",
			};
			RHITextureDesc depthOverrideProbeDesc = depthWriteProbeDesc;
			depthOverrideProbeDesc.m_DebugName = "Qualification.GraphicsDepthOverrideProbe";
			const RHITextureDesc markerDesc{
				.m_Format = colorFormat,
				.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::CopyDest,
				.m_Extent = { 2, 2, 1 },
				.m_DebugName = "Qualification.GraphicsMarker",
			};
			RHITextureOwner colorTexture(&device, device.CreateTexture({
				.m_Desc = colorDesc,
				.m_InitialState = UndefinedRHITextureState(),
				}));
			RHITextureOwner depthTexture(&device, device.CreateTexture({
				.m_Desc = depthTextureDesc,
				.m_InitialState = UndefinedRHITextureState(),
				}));
			RHITextureOwner markerTexture(&device, device.CreateTexture({
				.m_Desc = markerDesc,
				.m_InitialState = UndefinedRHITextureState(),
				}));
			RHITextureOwner depthWriteProbeTexture(&device, device.CreateTexture({
				.m_Desc = depthWriteProbeDesc,
				.m_InitialState = UndefinedRHITextureState(),
				}));
			RHITextureOwner depthOverrideProbeTexture(&device, device.CreateTexture({
				.m_Desc = depthOverrideProbeDesc,
				.m_InitialState = UndefinedRHITextureState(),
				}));
			const RHITextureViewHandle colorView = device.CreateTextureView(colorTexture.Get(), {
				.m_Type = RHITextureViewType::RenderTarget,
				.m_Dimension = RHITextureViewDimension::Texture2D,
				.m_Format = colorFormat,
				});
			const RHITextureViewHandle depthView = device.CreateTextureView(depthTexture.Get(), {
				.m_Type = RHITextureViewType::DepthStencil,
				.m_Dimension = RHITextureViewDimension::Texture2D,
				.m_Format = depthFormat,
				});
			const RHITextureViewHandle markerView = device.CreateTextureView(markerTexture.Get(), {
				.m_Type = RHITextureViewType::ShaderResource,
				.m_Dimension = RHITextureViewDimension::Texture2D,
				.m_Format = colorFormat,
				});
			const RHITextureViewHandle depthWriteProbeDsv =
				device.CreateTextureView(depthWriteProbeTexture.Get(), {
					.m_Type = RHITextureViewType::DepthStencil,
					.m_Dimension = RHITextureViewDimension::Texture2D,
					.m_Format = depthFormat,
					});
			const RHITextureViewHandle depthWriteProbeSrv =
				device.CreateTextureView(depthWriteProbeTexture.Get(), {
					.m_Type = RHITextureViewType::ShaderResource,
					.m_Dimension = RHITextureViewDimension::Texture2D,
					.m_Format = depthFormat,
					});
			const RHITextureViewHandle depthOverrideProbeDsv =
				device.CreateTextureView(depthOverrideProbeTexture.Get(), {
					.m_Type = RHITextureViewType::DepthStencil,
					.m_Dimension = RHITextureViewDimension::Texture2D,
					.m_Format = depthFormat,
					});
			const RHITextureViewHandle depthOverrideProbeSrv =
				device.CreateTextureView(depthOverrideProbeTexture.Get(), {
					.m_Type = RHITextureViewType::ShaderResource,
					.m_Dimension = RHITextureViewDimension::Texture2D,
					.m_Format = depthFormat,
					});
			const RHISamplerHandle markerSampler =
				device.CreateSampler({ .m_Filter = RHISamplerFilter::MinMagMipPoint });
			struct ViewCleanup
			{
				VulkanDevice& m_Device;
				std::array<RHITextureViewHandle, 7> m_Views;
				RHISamplerHandle m_Sampler;
				~ViewCleanup()
				{
					for (const RHITextureViewHandle view : m_Views)
					{
						if (view.IsValid())
						{
							m_Device.DestroyTextureView(view);
						}
					}
					if (m_Sampler.IsValid())
					{
						m_Device.DestroySampler(m_Sampler);
					}
				}
			} cleanup{ device, { colorView, depthView, markerView, depthWriteProbeDsv,
				depthWriteProbeSrv, depthOverrideProbeDsv, depthOverrideProbeSrv }, markerSampler };
			const RHIDescriptorHandle markerDescriptor =
				resources.GetTextureViewDescriptor(markerView);
			const RHIDescriptorHandle depthWriteProbeDescriptor =
				resources.GetTextureViewDescriptor(depthWriteProbeSrv);
			const RHIDescriptorHandle depthOverrideProbeDescriptor =
				resources.GetTextureViewDescriptor(depthOverrideProbeSrv);
			const RHIDescriptorHandle samplerDescriptor =
				resources.GetSamplerDescriptor(markerSampler);
			if (!colorTexture || !depthTexture || !markerTexture || !depthWriteProbeTexture ||
				!depthOverrideProbeTexture || !colorView.IsValid() || !depthView.IsValid() ||
				!markerView.IsValid() || !depthWriteProbeDsv.IsValid() ||
				!depthWriteProbeSrv.IsValid() || !depthOverrideProbeDsv.IsValid() ||
				!depthOverrideProbeSrv.IsValid() || !markerDescriptor.IsValid() ||
				!depthWriteProbeDescriptor.IsValid() || !depthOverrideProbeDescriptor.IsValid() ||
				!markerSampler.IsValid() || !samplerDescriptor.IsValid())
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: offscreen resource creation failed.");
				return 1;
			}

			TextureAssetData markerData = MakeQualificationTextureData(colorFormat, { 2, 2, 1 }, 8,
				{
					std::byte{ 0xFF }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0xFF },
					std::byte{ 0x00 }, std::byte{ 0xFF }, std::byte{ 0x00 }, std::byte{ 0xFF },
					std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0xFF }, std::byte{ 0xFF },
					std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0x00 }, std::byte{ 0xFF },
				});
			TransferManager transferManager(std::make_unique<VulkanTransferContext>(&device));
			{
				TransferBatch batch = transferManager.BeginBatch();
				const bool recorded = batch.UploadTexture(markerTexture.Get(), markerData.MakeUploadData(),
					UndefinedRHITextureState(), shaderResourceState);
				const RHITransferSubmission submission = batch.Submit(true);
				if (!recorded || !submission.m_Completion.IsValid() ||
					!device.IsFencePointCompleted(submission.m_Completion) ||
					!resources.PublishTextureViewDescriptor(markerView) ||
					!resources.PublishTextureViewDescriptor(depthWriteProbeSrv) ||
					!resources.PublishTextureViewDescriptor(depthOverrideProbeSrv) ||
					!resources.PublishSamplerDescriptor(markerSampler))
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
						"qualify graphics: marker upload or descriptor publication failed.");
					return 1;
				}
			}

			constexpr std::array vertices{
				CoordinateVertex{{-0.9f, 0.75f, 0.5f}, {0.0f, 0.0f}},
				CoordinateVertex{{-0.1f, -0.7f, 0.5f}, {1.0f, 1.0f}},
				CoordinateVertex{{-0.75f, -0.55f, 0.5f}, {0.0f, 1.0f}},
				CoordinateVertex{{0.1f, 0.75f, 0.5f}, {0.0f, 0.0f}},
				CoordinateVertex{{0.25f, -0.55f, 0.5f}, {0.0f, 1.0f}},
				CoordinateVertex{{0.9f, -0.7f, 0.5f}, {1.0f, 1.0f}},
				CoordinateVertex{{-1.0f, 1.0f, 0.5f}, {0.0f, 0.0f}},
				CoordinateVertex{{1.0f, 1.0f, 0.5f}, {1.0f, 0.0f}},
				CoordinateVertex{{1.0f, -1.0f, 0.5f}, {1.0f, 1.0f}},
				CoordinateVertex{{-1.0f, -1.0f, 0.5f}, {0.0f, 1.0f}},
			};
			constexpr std::array<uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };
			RHIBufferOwner vertexBuffer(&device, device.CreateBuffer({
				.m_SizeInBytes = sizeof(vertices),
				.m_StrideInBytes = sizeof(CoordinateVertex),
				.m_Usage = RHIBufferUsage::Vertex,
				.m_MemoryUsage = RHIMemoryUsage::CpuToGpu,
				.m_DebugName = "Qualification.GraphicsVertices",
				}));
			RHIBufferOwner indexBuffer(&device, device.CreateBuffer({
				.m_SizeInBytes = sizeof(indices),
				.m_StrideInBytes = sizeof(uint32_t),
				.m_Usage = RHIBufferUsage::Index,
				.m_MemoryUsage = RHIMemoryUsage::CpuToGpu,
				.m_DebugName = "Qualification.GraphicsIndices",
				}));
			if (!vertexBuffer || !indexBuffer)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: geometry buffer creation failed.");
				return 1;
			}
			void* mappedVertices = device.MapBuffer(vertexBuffer.Get(), {});
			void* mappedIndices = device.MapBuffer(indexBuffer.Get(), {});
			if (mappedVertices == nullptr || mappedIndices == nullptr)
			{
				if (mappedVertices)
				{
					device.UnmapBuffer(vertexBuffer.Get(), {});
				}
				if (mappedIndices)
				{
					device.UnmapBuffer(indexBuffer.Get(), {});
				}
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: geometry buffer creation failed.");
				return 1;
			}
			std::memcpy(mappedVertices, vertices.data(), sizeof(vertices));
			std::memcpy(mappedIndices, indices.data(), sizeof(indices));
			device.UnmapBuffer(vertexBuffer.Get(), { 0, sizeof(vertices) });
			device.UnmapBuffer(indexBuffer.Get(), { 0, sizeof(indices) });

			constexpr uint32_t computeInputValue = 7;
			constexpr uint32_t computeInitialValue = 1;
			constexpr uint32_t computeExpectedValue =
				computeInitialValue + computeInputValue * 2;
			const RHIBufferDesc computeInputDesc{
				.m_SizeInBytes = sizeof(uint32_t),
				.m_StrideInBytes = sizeof(uint32_t),
				.m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::CopyDest,
				.m_DebugName = "Qualification.ComputeInput",
			};
			const RHIBufferDesc computeOutputDesc{
				.m_SizeInBytes = sizeof(uint32_t),
				.m_StrideInBytes = sizeof(uint32_t),
				.m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::UnorderedAccess |
					RHIBufferUsage::CopyDest | RHIBufferUsage::CopySource,
				.m_DebugName = "Qualification.ComputeOutput",
			};
			const RHIBufferDesc computeReadbackDesc{
				.m_SizeInBytes = sizeof(uint32_t),
				.m_Usage = RHIBufferUsage::CopyDest,
				.m_MemoryUsage = RHIMemoryUsage::GpuToCpu,
				.m_DebugName = "Qualification.ComputeReadback",
			};
			RHIBufferOwner computeInput(&device, device.CreateBuffer(computeInputDesc));
			RHIBufferOwner computeOutput(&device, device.CreateBuffer(computeOutputDesc));
			RHIBufferOwner computeReadback(&device, device.CreateBuffer(computeReadbackDesc));
			if (!computeInput || !computeOutput || !computeReadback)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify compute: buffer creation failed.");
				return 1;
			}
			{
				TransferBatch batch = transferManager.BeginBatch();
				const bool inputRecorded = batch.UploadBuffer(computeInput.Get(), 0,
					&computeInputValue, sizeof(computeInputValue));
				const bool outputRecorded = batch.UploadBuffer(computeOutput.Get(), 0,
					&computeInitialValue, sizeof(computeInitialValue));
				const RHITransferSubmission submission = batch.Submit(true);
				if (!inputRecorded || !outputRecorded || !submission.m_Completion.IsValid() ||
					!device.IsFencePointCompleted(submission.m_Completion))
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify compute: buffer upload failed.");
					return 1;
				}
			}

			const uint32_t uniformAlignment = static_cast<uint32_t>(std::max<VkDeviceSize>(
				device.GetPhysicalDeviceLimits().minUniformBufferOffsetAlignment, 1));
			VulkanDynamicUniformBuffer uniformBuffer;
			if (!uniformBuffer.Initialize(&device, runtime.GetFrameSlotCount(), {
				.m_PageSizeInBytes = std::max(4096u, uniformAlignment * 8),
				.m_MaxPageCount = 2,
				.m_Alignment = 1,
				}))
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: dynamic uniform initialization failed.");
				return 1;
			}
			VulkanSet0DynamicUniformFrames set0Frames;
			if (!set0Frames.Initialize(
				&device, &uniformBuffer, runtime.GetFrameSlotCount()))
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: set-0 frame initialization failed.");
				return 1;
			}

			const VulkanBeginFrameResult begin = runtime.BeginFrame();
			if (!begin.IsAcquired() || !set0Frames.BeginFrame(begin.m_FrameSlotIndex))
			{
				if (begin.IsAcquired())
				{
					GGLAB_UNUSED(runtime.AbortFrame());
				}
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: frame acquire or set-0 begin failed.");
				return 1;
			}
			const VulkanFrameRecording recording = runtime.BeginFrameRecording();
			if (!recording.IsValid())
			{
				GGLAB_UNUSED(set0Frames.AbortFrame(begin.m_FrameSlotIndex));
				GGLAB_UNUSED(runtime.AbortFrame());
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: command recording did not begin.");
				return 1;
			}

			VkRenderingAttachmentInfo presentAttachment{};
			presentAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			presentAttachment.imageView = recording.m_BackBufferView;
			presentAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			presentAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			presentAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			presentAttachment.clearValue.color = { { 0.04f, 0.06f, 0.09f, 1.0f } };
			VkRenderingInfo presentRendering{};
			presentRendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			presentRendering.renderArea.extent = recording.m_Extent;
			presentRendering.layerCount = 1;
			presentRendering.colorAttachmentCount = 1;
			presentRendering.pColorAttachments = &presentAttachment;
			vkCmdBeginRendering(recording.m_CommandBuffer, &presentRendering);
			vkCmdEndRendering(recording.m_CommandBuffer);

			VulkanTexture* nativeColor = resources.ResolveTexture(colorTexture.Get());
			VulkanTexture* nativeDepth = resources.ResolveTexture(depthTexture.Get());
			VulkanTexture* nativeDepthWriteProbe =
				resources.ResolveTexture(depthWriteProbeTexture.Get());
			VulkanTexture* nativeDepthOverrideProbe =
				resources.ResolveTexture(depthOverrideProbeTexture.Get());
			if (nativeColor == nullptr || nativeDepth == nullptr ||
				nativeDepthWriteProbe == nullptr || nativeDepthOverrideProbe == nullptr)
			{
				GGLAB_UNUSED(set0Frames.AbortFrame(begin.m_FrameSlotIndex));
				GGLAB_UNUSED(runtime.AbortFrame());
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: native render targets are unavailable.");
				return 1;
			}
			const std::array beginBarriers{
				MakeVulkanImageBarrier(nativeColor->Get(), VK_IMAGE_ASPECT_COLOR_BIT,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
				MakeVulkanImageBarrier(nativeDepth->Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
						VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
						VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
				MakeVulkanImageBarrier(nativeDepthWriteProbe->Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
						VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
						VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
				MakeVulkanImageBarrier(nativeDepthOverrideProbe->Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
						VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
						VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
			};
			VkDependencyInfo beginDependency{};
			beginDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			beginDependency.imageMemoryBarrierCount = static_cast<uint32_t>(beginBarriers.size());
			beginDependency.pImageMemoryBarriers = beginBarriers.data();
			vkCmdPipelineBarrier2(recording.m_CommandBuffer, &beginDependency);

			VulkanGraphicsCommandContext commandContext(
				&device, &pipelineSystem, &uniformBuffer, &set0Frames);
			VulkanComputeCommandContext computeContext(commandContext);
			if (!commandContext.BeginEncoding(recording.m_CommandBuffer, begin.m_FrameSlotIndex))
			{
				GGLAB_UNUSED(set0Frames.AbortFrame(begin.m_FrameSlotIndex));
				GGLAB_UNUSED(runtime.AbortFrame());
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: graphics encoder did not begin.");
				return 1;
			}
			constexpr RHIResourceState commonBufferState{
				.m_Stages = RHIStage::All,
				.m_Access = RHIAccess::Common,
				.m_Layout = RHILayout::Common,
			};
			constexpr RHIResourceState computeReadState{
				.m_Stages = RHIStage::ComputeShader,
				.m_Access = RHIAccess::ShaderResource,
				.m_Layout = RHILayout::Common,
			};
			constexpr RHIResourceState computeReadWriteState{
				.m_Stages = RHIStage::ComputeShader,
				.m_Access = RHIAccess::UnorderedAccess,
				.m_Layout = RHILayout::Common,
			};
			constexpr RHIResourceState graphicsReadWriteState{
				.m_Stages = RHIStage::PixelShader,
				.m_Access = RHIAccess::UnorderedAccess,
				.m_Layout = RHILayout::Common,
			};
			constexpr RHIResourceState copySourceState{
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopySource,
				.m_Layout = RHILayout::Common,
			};
			constexpr RHIResourceState copyDestState{
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopyDest,
				.m_Layout = RHILayout::Common,
			};
			const std::array computeBeginBarriers{
				RHIBufferBarrier{ computeInput.Get(), commonBufferState, computeReadState },
				RHIBufferBarrier{ computeOutput.Get(), commonBufferState, computeReadWriteState },
				RHIBufferBarrier{ computeReadback.Get(), commonBufferState, copyDestState },
			};
			computeContext.BufferBarrier(computeBeginBarriers);
			computeContext.FlushBarriers();
			computeContext.SetPipeline(computePipeline);
			computeContext.SetReadOnlyBuffer(1, computeInput.Get());
			computeContext.SetReadWriteBuffer(2, computeOutput.Get());
			computeContext.Dispatch(1, 1, 1);
			const RHIBufferBarrier orderedStorageBarrier{
				computeOutput.Get(), computeReadWriteState, computeReadWriteState
			};
			computeContext.BufferBarrier(std::span(&orderedStorageBarrier, 1));
			computeContext.FlushBarriers();
			const RHIBufferBarrier computeToGraphicsBarrier{
				computeOutput.Get(), computeReadWriteState, graphicsReadWriteState
			};
			computeContext.BufferBarrier(std::span(&computeToGraphicsBarrier, 1));
			computeContext.FlushBarriers();
			const RHIBufferBarrier graphicsToComputeBarrier{
				computeOutput.Get(), graphicsReadWriteState, computeReadWriteState
			};
			computeContext.BufferBarrier(std::span(&graphicsToComputeBarrier, 1));
			computeContext.FlushBarriers();
			computeContext.Dispatch(1, 1, 1);
			const RHIBufferBarrier computeToCopyBarrier{
				computeOutput.Get(), computeReadWriteState, copySourceState
			};
			computeContext.BufferBarrier(std::span(&computeToCopyBarrier, 1));
			computeContext.FlushBarriers();
			computeContext.CopyBuffer(computeReadback.Get(), 0, computeOutput.Get(), 0,
				sizeof(uint32_t));
			const auto makeParameters = [&](CoordinateConformanceMode mode, float depth = 0.0f,
				uint32_t textureIndex = UINT32_MAX, float depthOverride = 0.0f)
				{
					return CoordinateConformanceParameters{
						.m_TextureIndex = textureIndex == UINT32_MAX
							? markerDescriptor.m_Index
							: textureIndex,
						.m_SamplerIndex = samplerDescriptor.m_Index,
						.m_Mode = mode,
						.m_Depth = depth,
						.m_TargetExtent = {
							static_cast<float>(targetWidth), static_cast<float>(targetHeight),
						},
						.m_DepthOverride = depthOverride,
					};
				};

			const RHIRenderingAttachment depthWriteProbeAttachment{
				.m_View = depthWriteProbeDsv,
				.m_LoadOp = RHIContentLoadOp::DontCare,
			};
			commandContext.BeginRendering({ .m_DepthAttachment = depthWriteProbeAttachment });
			commandContext.ClearDepthAttachment(0.0f);
			commandContext.SetPipeline(depthWithoutPixelPipeline);
			commandContext.SetViewport({ 0.0f, 0.0f,
				static_cast<float>(targetWidth), static_cast<float>(targetHeight) });
			commandContext.SetScissorRect({ 0, 0,
				static_cast<int32_t>(targetWidth), static_cast<int32_t>(targetHeight) });
			commandContext.SetPushConstants(0, makeParameters(
				CoordinateConformanceMode::Winding, DepthOnlyExpectedDepth));
			commandContext.DrawFullscreenTriangle();
			commandContext.EndRendering();

			const RHIRenderingAttachment depthOverrideProbeAttachment{
				.m_View = depthOverrideProbeDsv,
				.m_LoadOp = RHIContentLoadOp::DontCare,
			};
			commandContext.BeginRendering({ .m_DepthAttachment = depthOverrideProbeAttachment });
			commandContext.ClearDepthAttachment(0.0f);
			commandContext.SetPipeline(depthOverridePipeline);
			commandContext.SetViewport({ 0.0f, 0.0f,
				static_cast<float>(targetWidth), static_cast<float>(targetHeight) });
			commandContext.SetScissorRect({ 0, 0,
				static_cast<int32_t>(targetWidth), static_cast<int32_t>(targetHeight) });
			commandContext.SetPushConstants(0, makeParameters(CoordinateConformanceMode::Winding,
				DepthOnlyVertexDepth, UINT32_MAX, DepthOnlyExpectedDepth));
			commandContext.DrawFullscreenTriangle();
			commandContext.EndRendering();

			const std::array depthProbeBarriers{
				MakeVulkanImageBarrier(nativeDepthWriteProbe->Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
						VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
					VK_ACCESS_2_SHADER_SAMPLED_READ_BIT),
				MakeVulkanImageBarrier(nativeDepthOverrideProbe->Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
						VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
					VK_ACCESS_2_SHADER_SAMPLED_READ_BIT),
			};
			VkDependencyInfo depthProbeDependency{};
			depthProbeDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			depthProbeDependency.imageMemoryBarrierCount =
				static_cast<uint32_t>(depthProbeBarriers.size());
			depthProbeDependency.pImageMemoryBarriers = depthProbeBarriers.data();
			vkCmdPipelineBarrier2(recording.m_CommandBuffer, &depthProbeDependency);

			const std::array attachments{
				RHIRenderingAttachment{.m_View = colorView, .m_LoadOp = RHIContentLoadOp::DontCare },
			};
			const RHIRenderingAttachment depthAttachment{
				.m_View = depthView,
				.m_LoadOp = RHIContentLoadOp::DontCare,
			};
			commandContext.BeginRendering({
				.m_ColorAttachments = attachments,
				.m_DepthAttachment = depthAttachment,
				});
			commandContext.ClearColorAttachment(0, { 0.01f, 0.015f, 0.025f, 1.0f });
			commandContext.ClearDepthAttachment(0.0f);
			const RHIVertexBufferBinding vertexBinding{
				.m_Buffer = vertexBuffer.Get(),
				.m_Stride = sizeof(CoordinateVertex),
				.m_SizeInBytes = sizeof(vertices),
			};
			commandContext.SetVertexBuffers(
				0, std::span<const RHIVertexBufferBinding>(&vertexBinding, 1));
			commandContext.SetPipeline(geometryPipeline);
			commandContext.SetViewport({ 0.0f, 0.0f, 32.0f, 32.0f });
			commandContext.SetScissorRect({ 0, 0, 32, 32 });
			commandContext.SetPushConstants(0, makeParameters(CoordinateConformanceMode::Winding));
			commandContext.Draw(6);

			commandContext.SetPipeline(backCullPipeline);
			commandContext.SetViewport({ 32.0f, 32.0f, 8.0f, 16.0f });
			commandContext.SetScissorRect({ 32, 32, 40, 48 });
			commandContext.SetPushConstants(0, makeParameters(CoordinateConformanceMode::Winding));
			commandContext.Draw(3, 1, 3);
			commandContext.SetViewport({ 32.0f, 48.0f, 8.0f, 16.0f });
			commandContext.SetScissorRect({ 32, 48, 40, 64 });
			commandContext.Draw(3);

			const RHIIndexBufferBinding indexBinding{
				.m_Buffer = indexBuffer.Get(),
				.m_SizeInBytes = sizeof(indices),
				.m_Format = RHIFormat::R32Uint,
			};
			commandContext.SetIndexBuffer(indexBinding);
			commandContext.SetViewport({ 32.0f, 0.0f, 32.0f, 32.0f });
			commandContext.SetScissorRect({ 32, 0, 64, 32 });
			commandContext.SetPushConstants(
				0, makeParameters(CoordinateConformanceMode::MarkerSampling));
			commandContext.DrawIndexed(6, 1, 0, 6, 0);

			commandContext.SetPipeline(depthPipeline);
			commandContext.SetViewport({ 0.0f, 32.0f, 32.0f, 32.0f });
			commandContext.SetScissorRect({ 0, 32, 32, 64 });
			commandContext.SetPushConstants(0, makeParameters(
				CoordinateConformanceMode::DepthVisualization, ReversedZFarProbeDepth));
			commandContext.DrawFullscreenTriangle();
			commandContext.SetPushConstants(0, makeParameters(
				CoordinateConformanceMode::DepthVisualization, ReversedZNearProbeDepth));
			commandContext.DrawFullscreenTriangle();

			commandContext.SetPipeline(positionPipeline);
			commandContext.SetViewport({ 0.0f, 56.0f, 8.0f, 8.0f });
			commandContext.SetScissorRect({ 0, 56, 8, 64 });
			commandContext.SetPushConstants(
				0, makeParameters(CoordinateConformanceMode::DepthProbe, 0.0f,
					depthWriteProbeDescriptor.m_Index, DepthOnlyExpectedDepth));
			commandContext.DrawFullscreenTriangle();
			commandContext.SetViewport({ 8.0f, 56.0f, 8.0f, 8.0f });
			commandContext.SetScissorRect({ 8, 56, 16, 64 });
			commandContext.SetPushConstants(
				0, makeParameters(CoordinateConformanceMode::DepthProbe, 0.0f,
					depthOverrideProbeDescriptor.m_Index, DepthOnlyExpectedDepth));
			commandContext.DrawFullscreenTriangle();

			commandContext.SetViewport({ 32.0f, 32.0f, 32.0f, 32.0f });
			commandContext.SetScissorRect({ 40, 40, 56, 56 });
			commandContext.SetPushConstants(0, makeParameters(CoordinateConformanceMode::Position));
			commandContext.DrawFullscreenTriangle();
			commandContext.EndRendering();
			const bool encodingSucceeded = commandContext.FinishEncoding();
			std::vector<RHITextureHandle> usedTextures(
				commandContext.GetUsedTextures().begin(), commandContext.GetUsedTextures().end());
			std::vector<RHIBufferHandle> usedBuffers(
				commandContext.GetUsedBuffers().begin(), commandContext.GetUsedBuffers().end());
			if (!encodingSucceeded)
			{
				GGLAB_UNUSED(set0Frames.AbortFrame(begin.m_FrameSlotIndex));
				GGLAB_UNUSED(runtime.AbortFrame());
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: graphics command validation failed.");
				return 1;
			}

			const VkImageMemoryBarrier2 toCommon = MakeVulkanImageBarrier(nativeColor->Get(),
				VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
				VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
			VkDependencyInfo endDependency{};
			endDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			endDependency.imageMemoryBarrierCount = 1;
			endDependency.pImageMemoryBarriers = &toCommon;
			vkCmdPipelineBarrier2(recording.m_CommandBuffer, &endDependency);
			if (!runtime.EndFrameRecording())
			{
				GGLAB_UNUSED(set0Frames.AbortFrame(begin.m_FrameSlotIndex));
				GGLAB_UNUSED(runtime.AbortFrame());
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: command recording did not finalize.");
				return 1;
			}
			const VulkanSubmitPresentResult frame = runtime.EndFrame();
			if (!frame.m_Submitted || !frame.m_SubmittedFencePoint.IsValid())
			{
				GGLAB_UNUSED(set0Frames.AbortFrame(begin.m_FrameSlotIndex));
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: graphics submission failed.");
				return 1;
			}
			const bool uniformFrameEnded =
				set0Frames.EndFrame(begin.m_FrameSlotIndex, frame.m_SubmittedFencePoint);
			for (const RHITextureHandle texture : usedTextures)
			{
				resources.RecordTextureUse(texture, frame.m_SubmittedFencePoint);
			}
			for (const RHIBufferHandle buffer : usedBuffers)
			{
				resources.RecordBufferUse(buffer, frame.m_SubmittedFencePoint);
			}
			pipelineSystem.Clear();
			const bool pipelineCacheCleared = pipelineSystem.IsAlive(layoutHandle) &&
				!pipelineSystem.IsAlive(geometryPipeline) &&
				!pipelineSystem.IsAlive(depthWithoutPixelPipeline) &&
				!pipelineSystem.IsAlive(depthOverridePipeline) &&
				!pipelineSystem.IsAlive(computePipeline);
			if (!uniformFrameEnded || !pipelineCacheCleared || runtime.WaitIdle() != VK_SUCCESS)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify graphics: completion, pipeline invalidation or uniform retirement failed.");
				return 1;
			}
			const auto* computeReadbackValue = static_cast<const uint32_t*>(
				device.MapBuffer(computeReadback.Get(), { 0, sizeof(uint32_t) }));
			const bool computePassed =
				computeReadbackValue && *computeReadbackValue == computeExpectedValue;
			if (computeReadbackValue)
			{
				device.UnmapBuffer(computeReadback.Get(), {});
			}
			if (!computePassed)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify compute: dispatch, ordered-storage barrier or copy result failed.");
				return 1;
			}

			TextureAssetData readback;
			{
				TransferBatch batch = transferManager.BeginBatch();
				RHITextureReadbackRequest request =
					batch.ReadbackTexture(colorTexture.Get(), colorDesc);
				const RHITransferSubmission submission = batch.Submit(true);
				if (!request.IsValid() || !submission.m_Completion.IsValid() ||
					!device.IsFencePointCompleted(submission.m_Completion))
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: color readback failed.");
					return 1;
				}
				const std::byte* mapped = transferManager.MapTextureReadback(device, request);
				readback = transferManager.ResolveMappedTextureReadback(request, mapped);
				transferManager.UnmapTextureReadback(device, request);
				if (!readback.IsValid())
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("qualify graphics: color readback failed.");
					return 1;
				}
			}

			struct Pixel
			{
				uint8_t m_R = 0;
				uint8_t m_G = 0;
				uint8_t m_B = 0;
				uint8_t m_A = 0;
			};
			const auto readPixel = [&readback](uint32_t x, uint32_t y) noexcept
				{
					const size_t offset = (static_cast<size_t>(y) * targetWidth + x) * 4;
					return Pixel{
						std::to_integer<uint8_t>(readback.m_Pixels[offset + 0]),
						std::to_integer<uint8_t>(readback.m_Pixels[offset + 1]),
						std::to_integer<uint8_t>(readback.m_Pixels[offset + 2]),
						std::to_integer<uint8_t>(readback.m_Pixels[offset + 3]),
					};
				};
			const auto nearPixel = [](Pixel actual, Pixel expected, uint8_t tolerance) noexcept
				{
					const auto channelNear = [tolerance](uint8_t a, uint8_t b) noexcept
						{
							return std::abs(static_cast<int>(a) - static_cast<int>(b)) <= tolerance;
						};
					return channelNear(actual.m_R, expected.m_R) &&
						channelNear(actual.m_G, expected.m_G) &&
						channelNear(actual.m_B, expected.m_B) &&
						channelNear(actual.m_A, expected.m_A);
				};
			const std::array probePixels{
				readPixel(7, 19), readPixel(23, 19),
				readPixel(38, 42), readPixel(34, 58),
				readPixel(40, 8), readPixel(56, 8), readPixel(40, 24), readPixel(56, 24),
				readPixel(16, 48), readPixel(4, 60), readPixel(12, 60),
				readPixel(48, 48), readPixel(33, 33),
			};
			const bool pixelsPassed =
				nearPixel(probePixels[0], { 26, 230, 51, 255 }, 20) &&
				nearPixel(probePixels[1], { 230, 26, 204, 255 }, 20) &&
				nearPixel(probePixels[2], { 3, 4, 6, 255 }, 10) &&
				nearPixel(probePixels[3], { 26, 230, 51, 255 }, 20) &&
				nearPixel(probePixels[4], { 255, 0, 0, 255 }, 20) &&
				nearPixel(probePixels[5], { 0, 255, 0, 255 }, 20) &&
				nearPixel(probePixels[6], { 0, 0, 255, 255 }, 20) &&
				nearPixel(probePixels[7], { 255, 255, 0, 255 }, 20) &&
				nearPixel(probePixels[8], { 26, 230, 51, 255 }, 20) &&
				nearPixel(probePixels[9], { 26, 230, 51, 255 }, 20) &&
				nearPixel(probePixels[10], { 26, 230, 51, 255 }, 20) &&
				nearPixel(probePixels[11], { 193, 193, 62, 255 }, 20) &&
				nearPixel(probePixels[12], { 3, 4, 6, 255 }, 10);
			if (!pixelsPassed)
			{
				std::string observed;
				for (const Pixel pixel : probePixels)
				{
					observed += std::format(" ({},{},{},{})", pixel.m_R, pixel.m_G,
						pixel.m_B, pixel.m_A);
				}
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
					"qualify graphics: coordinate probe pixels did not match:{}", observed));
				return 1;
			}

			set0Frames.Finalize();
			uniformBuffer.Finalize();
			transferManager.Reclaim();
			device.RetireCompletedWork();
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(
				"qualify graphics/compute: command-layer barriers, buffer copy, ordered storage dispatch, winding, back-face culling, depth-only shader stages, indexed texture sampling, reversed-Z, scissor and SV_Position probes passed.");
			return 0;
		}

		// Runs the minimal-frame qualification script: continuous presents,
		// first-image abort, already-presented abort, normal/abort
		// alternation, continuous abort, resize, minimize/restore and VSync
		// switching. Every step keeps the partial application command buffer
		// unsubmitted; AbortFrame uses the dedicated minimal command buffer.
		[[nodiscard]] int RunVulkanQualificationFrames(
			VulkanFrameRuntime& runtime, HWND hwnd,
			const std::filesystem::path& shaderSourceRoot,
			const std::filesystem::path& shaderCacheRoot) noexcept
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

			GGLAB_LOG_GRAPHICS_INFO_ALWAYS("Vulkan minimal-frame qualification started.");

			// Establish a run of continuous normal presents.
			for (uint32_t i = 0; i < 4; ++i)
			{
				if (!runNormal())
				{
					return 1;
				}
			}
			// Deferred-retirement probe: the gate value is reserved, not
			// submitted, so the slot must stay occupied; the submissions
			// later submissions complete the gate.
			VulkanDeferredRetirementProbeState deferredPending;
			if (RunVulkanDeferredRetirementProbe(
				*runtime.GetDevice(), runtime, deferredPending) != 0)
			{
				return 1;
			}
			// Exercise consecutive aborts.
			for (uint32_t i = 0; i < 3; ++i)
			{
				if (!runAbort())
				{
					return 1;
				}
			}
			// Alternate normal and aborted frames.
			for (uint32_t i = 0; i < 4; ++i)
			{
				if (!runNormal() || !runAbort())
				{
					return 1;
				}
			}
			// Exercise grouped frames ending in aborts.
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

			// Exercise real window resize. Every recreate is followed by an
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

			// Toggle VSync on and off. The actual present mode is logged after
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

			// Exercise minimize and restore. A minimized window has a zero
			// drawable extent: BeginFrame must never be called, no zero-size
			// swapchain is created, and restore recreates at the real extent.
			ShowWindow(hwnd, SW_MINIMIZE);
			{
				RECT minimizedRect{};
				GetClientRect(hwnd, &minimizedRect);
				if (minimizedRect.right - minimizedRect.left != 0 ||
					minimizedRect.bottom - minimizedRect.top != 0)
				{
					GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
						"Minimized window did not report a zero drawable extent.");
					return 1;
				}
				GGLAB_LOG_GRAPHICS_INFO_ALWAYS(
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
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"qualify WaitIdle failed before the final summary; the runtime may have "
					"entered the fatal state.");
				return 1;
			}
			if (RunVulkanDescriptorPipelineQualification(*runtime.GetDevice(), runtime,
				shaderSourceRoot, shaderCacheRoot) != 0)
			{
				return 1;
			}

			// The submissions after the probe passed the reserved gate
			// value: the pending slot must now be released and reusable.
			if (RunVulkanDeferredRetirementRelease(
				*runtime.GetDevice(), deferredPending) != 0)
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
			if (RunVulkanTransferQualification(*runtime.GetDevice(), runtime) != 0)
			{
				return 1;
			}
			if (RunVulkanGraphicsQualification(*runtime.GetDevice(), runtime,
				shaderSourceRoot, shaderCacheRoot) != 0)
			{
				return 1;
			}

			const auto& swapChain = runtime.GetSwapChain();
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
				"qualify summary: normal={} abort={} recreate={} mismatchedFrames={} "
				"suboptimal={} timeline={}",
				stats.m_NormalFrames, stats.m_AbortFrames, stats.m_RecreateCount,
				stats.m_MismatchedFrames, stats.m_SuboptimalCount,
				runtime.GetTimelineSignalValue()));
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
				"qualify swapchain: format={} extent={}x{} presentMode={} vsync={} images={} "
				"frameSlots={}",
				GetRHIFormatInfo(swapChain.GetFormat()).m_Name, swapChain.GetWidth(),
				swapChain.GetHeight(), PresentModeName(swapChain.GetPresentMode()),
				runtime.GetVsync() ? "on" : "off", swapChain.GetImageCount(),
				runtime.GetFrameSlotCount()));
			GGLAB_LOG_GRAPHICS_INFO_ALWAYS("Vulkan minimal-frame qualification finished.");
			return 0;
		}
#endif
	}

	int RunVulkanQualification(const VulkanQualificationOptions& options) noexcept
	{
#if GGLAB_ENABLE_VULKAN
		if (!options.IsConfigurationValid())
		{
			if (!options.HasRequiredNativeSurfaceHandles())
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"Vulkan qualification requires non-null HINSTANCE and HWND surface handles.");
			}
			else
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"Vulkan frame qualification requires non-empty shader source and cache roots.");
			}
			return 1;
		}

		VulkanWin32SurfaceFactory surfaceFactory(options.m_HInstance, options.m_Hwnd);
		if (options.m_ListAdapters)
		{
			// Inspection-only: enumerate, evaluate and log every adapter,
			// then exit without creating a frame runtime.
			VulkanBootstrapReport report;
			return RunVulkanBootstrap(
				MakeVulkanBootstrapOptions(options, surfaceFactory), report);
		}

		RECT clientRect{};
		if (!GetClientRect(options.m_Hwnd, &clientRect) ||
			clientRect.right - clientRect.left == 0 ||
			clientRect.bottom - clientRect.top == 0)
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("Vulkan startup requires a nonzero window drawable extent.");
			return 1;
		}

		VulkanBootstrapRuntimeCreateInfo createInfo{};
		createInfo.m_BootstrapOptions = MakeVulkanBootstrapOptions(options, surfaceFactory);
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
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
				"Vulkan bootstrap runtime creation failed: {}", result.m_Error));
			return 1;
		}
		GGLAB_LOG_GRAPHICS_INFO_ALWAYS(std::format(
			"Vulkan qualification on adapter [{}] '{}' (validation={}).",
			result.m_SelectedSnapshot.m_Identity.m_EnumerationIndex,
			result.m_SelectedSnapshot.m_Identity.m_DeviceName,
			result.m_HasDebugMessenger ? "enabled" : "disabled"));

		const int exitCode = RunVulkanQualificationFrames(*result.m_FrameRuntime, options.m_Hwnd,
			options.m_ShaderSourceRoot, options.m_ShaderCacheRoot);
		return exitCode;
#else
		GGLAB_UNUSED(options);
		GGLAB_LOG_GRAPHICS_ERROR_ALWAYS("Vulkan backend was not built (GGLAB_ENABLE_VULKAN=0).");
		return 1;
#endif
	}
}
