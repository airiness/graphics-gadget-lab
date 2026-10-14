#include "NonFiniteScanNumericSelfTests.h"
#include "Compiler/ShaderCompiler.h"
#include "DevelopmentShaderPaths.h"
#include "Targets/DX12ShaderTarget.h"
#include "Targets/Vulkan13ShaderTarget.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/DX12/DX12ContextFactory.h"
#include "GGLabRuntime/Graphics/RHI/Vulkan/VulkanWin32ContextFactory.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/Shader/ShaderTypes.h"
#include "GGLabTestCore/SelfTest.h"

#include <Windows.h>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace gglab
{
	namespace
	{
		class HiddenScanWindow
		{
		public:
			HiddenScanWindow() noexcept : m_Window(CreateWindowExW(0, L"STATIC", L"GGLab non-finite numeric test",
				WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)) {}
			~HiddenScanWindow() { if (m_Window) { DestroyWindow(m_Window); } }
			HiddenScanWindow(const HiddenScanWindow&) = delete;
			HiddenScanWindow& operator=(const HiddenScanWindow&) = delete;
			HWND m_Window = nullptr;
		};

		void BufferTransition(RHICommandContext& commands, RHIBufferHandle buffer,
			RHIResourceState before, RHIResourceState after) noexcept
		{
			// Test-owned resources are outside RenderGraph. This fixture owns their
			// transitions and completion, including clear->scan UAV ordering.
			const RHIBufferBarrier barrier{ buffer, before, after };
			commands.BufferBarrier(std::span(&barrier, 1));
			commands.FlushBarriers();
		}

		void RunGpuTests(SelfTestContext& context, RHIBackendType backend) noexcept
		{
			const auto root = win32::GetExecutableDirectory();
			ShaderCompiler compiler(ResolveShaderSourceRoot(root), ResolveShaderCacheRoot(root));
			ShaderDesc shader{ .m_SourcePath = L"Passes/PassNonFiniteScan.hlsl", .m_Stage = ShaderStage::Compute,
				.m_Target = backend == RHIBackendType::DX12 ? MakeDX12CompileTarget(ShaderStage::Compute) : MakeVulkan13CompileTarget(ShaderStage::Compute),
				.m_Entry = L"CSClear", .m_IncludeDirs = { L"." } };
			const auto clear = compiler.Compile(shader);
			shader.m_Entry = L"CSMain";
			shader.m_Defines = { { L"GGLAB_NONFINITE_SCAN_BUFFER", L"1" } };
			const auto scan = compiler.Compile(shader);
			context.Check(clear.IsSuccess() && scan.IsSuccess(), "Production scan and clear shaders compile for the requested backend");
			if (!clear.IsSuccess() || !scan.IsSuccess())
			{
				return;
			}
			HiddenScanWindow window;
			context.Check(window.m_Window != nullptr, "Non-finite scan fixture creates a hidden window");
			if (!window.m_Window)
			{
				return;
			}
			const RHIContextDesc desc{ .m_Width = 64, .m_Height = 64, .m_EnableDebugValidation = true };
			std::unique_ptr<RHIContext> rhi;
			if (backend == RHIBackendType::DX12)
			{
				rhi = CreateDX12Context(desc, window.m_Window);
			}
#if GGLAB_ENABLE_VULKAN
			else
			{
				rhi = CreateVulkanWin32Context(desc, GetModuleHandleW(nullptr), window.m_Window, sizeof(void*) == 8);
			}
#endif
			context.Check(rhi != nullptr, "Non-finite numeric RHI initializes without fallback");
			if (!rhi)
			{
				return;
			}
			auto& device = rhi->GetDevice();
			std::printf("Non-finite scan adapter: %s\n", std::string(device.GetAdapterCompatibilityIdentity()).c_str());
			constexpr std::array fields{
				GpuBufferFieldLayout{ "flags", 0, GpuBufferScalar::Uint32 },
				GpuBufferFieldLayout{ "position", 4, GpuBufferScalar::Float32, 2 },
				GpuBufferFieldLayout{ "halfPair", 12, GpuBufferScalar::Float16, 2 },
			};
			constexpr uint32_t capacity = 257;
			std::array<uint32_t, capacity * 5> words;
			for (uint32_t i = 0; i < capacity; ++i)
			{
				words[i * 5] = 0x7fc00001u; // Integer flags deliberately look like NaN.
				words[i * 5 + 1] = std::bit_cast<uint32_t>(2.0f);
				words[i * 5 + 2] = std::bit_cast<uint32_t>(-1.0f);
				words[i * 5 + 3] = 0xb4003c00u; // (1, -0.25) in two half words.
				words[i * 5 + 4] = 0x7f800000u; // Padding deliberately looks like Inf.
				if (i >= 201)
				{
					words[i * 5 + 1] = 0x7fc00001u; // Inactive capacity must not be scanned.
				}
			}
			words[9 * 5 + 1] = 0x7fc00001u;
			words[77 * 5 + 2] = 0x7f800000u;
			words[199 * 5 + 2] = 0xff800000u;
			words[37 * 5 + 3] = 0x7e003c00u;
			const auto layout = BuildNonFiniteBufferScanLayout({ 20, fields }, sizeof(words), 201);
			const auto upload = [&](const void* data, uint64_t bytes, uint32_t stride)
				{
					RHIBufferOwner buffer(&device, device.CreateBuffer({ .m_SizeInBytes = bytes, .m_StrideInBytes = stride,
						.m_Usage = RHIBufferUsage::Structured, .m_MemoryUsage = RHIMemoryUsage::CpuToGpu, .m_DebugName = "Numeric.ScanInput" }));
					if (buffer)
					{
						if (void* mapped = device.MapBuffer(buffer.Get(), {}))
						{
							std::memcpy(mapped, data, static_cast<size_t>(bytes));
							device.UnmapBuffer(buffer.Get(), { 0, bytes });
						}
						else { buffer.Reset(); }
					}
					return buffer;
				};
			auto source = upload(words.data(), sizeof(words), 0);
			auto components = upload(layout.m_Components.data(), layout.m_Components.size() * sizeof(NonFiniteBufferComponent), sizeof(NonFiniteBufferComponent));
			RHIBufferOwner record(&device, device.CreateBuffer({ .m_SizeInBytes = sizeof(NonFiniteScanRecord),
				.m_StrideInBytes = sizeof(NonFiniteScanRecord), .m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::UnorderedAccess | RHIBufferUsage::CopySource,
				.m_DebugName = "Numeric.ScanRecord" }));
			constexpr std::array<uint32_t, 3> elementCounts{ 9, 201, 0 };
			RHIBufferOwner readback(&device, device.CreateBuffer({ .m_SizeInBytes = sizeof(NonFiniteScanRecord) * elementCounts.size(),
				.m_Usage = RHIBufferUsage::CopyDest, .m_MemoryUsage = RHIMemoryUsage::GpuToCpu, .m_DebugName = "Numeric.ScanReadback" }));
			auto& pipelines = rhi->GetPipelineSystem();
			RHIBindingLayoutDesc bindings;
			bindings.m_Slots[0] = { .m_Type = RHIBindingType::PushConstants, .m_Visibility = RHIShaderStage::Compute, .m_Binding = 2, .m_SizeInBytes = sizeof(NonFiniteScanParameters) };
			bindings.m_Slots[1] = { .m_Type = RHIBindingType::ReadOnlyStorageBuffer, .m_Visibility = RHIShaderStage::Compute, .m_Binding = 0 };
			bindings.m_Slots[2] = { .m_Type = RHIBindingType::ReadOnlyStorageBuffer, .m_Visibility = RHIShaderStage::Compute, .m_Binding = 1 };
			bindings.m_Slots[3] = { .m_Type = RHIBindingType::ReadWriteStorageBuffer, .m_Visibility = RHIShaderStage::Compute, .m_Binding = 0 };
			bindings.m_SlotCount = 4;
			const auto bindingLayout = pipelines.CreateBindingLayout(bindings);
			const auto pipeline = [&](const ShaderCompileResult& compiled, std::string_view entry)
				{
					const auto& artifact = compiled.m_Artifact;
					return pipelines.CreateComputePipeline({ .m_Desc = { .m_BindingLayout = bindingLayout },
						.m_ComputeShader = { artifact.m_Binary.Data(), artifact.m_Binary.SizeInBytes(), artifact.GetBinaryFormat(),
							ComputeShaderBinaryHash(artifact.m_Binary, artifact.GetBinaryFormat()), entry } });
				};
			const auto clearPipeline = bindingLayout.IsValid() ? pipeline(clear, "CSClear") : RHIPipelineHandle{};
			const auto scanPipeline = bindingLayout.IsValid() ? pipeline(scan, "CSMain") : RHIPipelineHandle{};
			context.Check(source && components && record && readback && clearPipeline.IsValid() && scanPipeline.IsValid(),
				"Scan fixture resources and pipelines are available");
			if (!source || !components || !record || !readback || !clearPipeline.IsValid() || !scanPipeline.IsValid())
			{
				return;
			}
			const auto begin = rhi->BeginFrame();
			context.Check(begin.IsReady(), "Hidden non-finite scan frame begins");
			if (!begin.IsReady())
			{
				return;
			}
			auto& commands = begin.GetFrame()->GetDirectComputeContext();
			const RHIResourceState uav{ RHIStage::ComputeShader, RHIAccess::UnorderedAccess, RHILayout::UnorderedAccess };
			const RHIResourceState copySource{ RHIStage::Copy, RHIAccess::CopySource, RHILayout::Common };
			const RHIResourceState copyDest{ RHIStage::Copy, RHIAccess::CopyDest, RHILayout::Common };
			BufferTransition(commands, readback.Get(), CommonRHIResourceState(), copyDest);
			for (uint32_t test = 0; test < elementCounts.size(); ++test)
			{
				BufferTransition(commands, record.Get(), test == 0 ? CommonRHIResourceState() : copySource, uav);
				commands.SetPipeline(clearPipeline);
				commands.SetReadWriteBuffer(3, record.Get());
				commands.Dispatch(1, 1, 1);
				BufferTransition(commands, record.Get(), uav, uav);
				commands.SetPipeline(scanPipeline);
				commands.SetReadOnlyBuffer(1, source.Get());
				commands.SetReadOnlyBuffer(2, components.Get());
				commands.SetReadWriteBuffer(3, record.Get());
				const uint32_t count = elementCounts[test] * 4;
				commands.SetPushConstants(0, NonFiniteScanParameters{ .m_ScalarCount = count,
					.m_DispatchGroupsX = 1, .m_BufferStride = 20, .m_ComponentCount = 4 });
				// Exercise the y dimension and the partial final wave, not only a 1D row.
				commands.Dispatch(1, count == 0 ? 1 : (count + 63) / 64, 1);
				BufferTransition(commands, record.Get(), uav, copySource);
				commands.CopyBuffer(readback.Get(), test * sizeof(NonFiniteScanRecord), record.Get(), 0, sizeof(NonFiniteScanRecord));
			}
			// Give the hidden host a defined backbuffer before presentation.
			auto& graphics = begin.GetFrame()->GetGraphicsContext();
			const auto backbuffer = begin.GetFrame()->GetBackBuffer();
			const auto rtv = device.CreateTextureView(backbuffer, { .m_Type = RHITextureViewType::RenderTarget, .m_Dimension = RHITextureViewDimension::Texture2D });
			const RHIResourceState renderTarget{ RHIStage::RenderTarget, RHIAccess::RenderTarget, RHILayout::RenderTarget };
			RHITextureBarrier barrier{ backbuffer, UndefinedRHITextureState(), renderTarget };
			graphics.TextureBarrier(std::span(&barrier, 1));
			graphics.FlushBarriers();
			const RHIRenderingAttachment attachment{ .m_View = rtv, .m_LoadOp = RHIContentLoadOp::DontCare };
			graphics.BeginRendering({ .m_ColorAttachments = std::span(&attachment, 1) });
			graphics.ClearColorAttachment(0, { 0, 0, 0, 1 });
			graphics.EndRendering();
			barrier = { backbuffer, renderTarget, PresentRHITextureState() };
			graphics.TextureBarrier(std::span(&barrier, 1));
			graphics.FlushBarriers();
			const auto end = rhi->EndFrame(*begin.GetFrame());
			rhi->WaitIdle();
			const bool completed = end.IsCompleted() && end.HasSubmission() && device.IsFencePointCompleted(end.GetSubmittedFence());
			context.Check(completed, "Scan fence completes before mapping or resource destruction");
			device.DestroyTextureView(rtv);
			if (completed)
			{
				const auto* mapped = static_cast<const std::byte*>(device.MapBuffer(readback.Get(), { 0, sizeof(NonFiniteScanRecord) * elementCounts.size() }));
				context.Check(mapped != nullptr, "Completed scan readback maps");
				if (mapped)
				{
					for (uint32_t test = 0; test < elementCounts.size(); ++test)
					{
						NonFiniteScanRecord actual;
						std::memcpy(&actual, mapped + test * sizeof(actual), sizeof(actual));
						NonFiniteScanRecord expected;
						for (uint32_t i = 0; i < elementCounts[test]; ++i)
						{
							AccumulateNonFiniteScanValue(expected, words[i * 5 + 1], i * 4);
							AccumulateNonFiniteScanValue(expected, words[i * 5 + 2], i * 4 + 1);
							AccumulateNonFiniteScanValue(expected, std::bit_cast<uint32_t>(1.0f), i * 4 + 2);
							AccumulateNonFiniteScanValue(expected, i == 37 ? 0x7fc00000u : std::bit_cast<uint32_t>(-0.25f), i * 4 + 3);
						}
						context.Check(std::memcmp(&actual, &expected, sizeof(actual)) == 0,
							"GPU counts, extrema, clear sentinels and earliest index exactly match the CPU ABI reference");
						context.Check(EvaluateNonFiniteBoundary(actual, elementCounts[test] * 4).m_Claim.m_Status ==
							(test == 1 ? NonFiniteClaimStatus::Failed : NonFiniteClaimStatus::Passed),
							"GPU result respects valid range, float-only layout and empty-range coverage");
					}
					device.UnmapBuffer(readback.Get(), {});
				}
			}
			rhi->RetireCompletedWork();
		}
	}

	void RunDX12NonFiniteScanNumericSelfTests(SelfTestContext& context) noexcept { RunGpuTests(context, RHIBackendType::DX12); }
	void RunVulkanNonFiniteScanNumericSelfTests(SelfTestContext& context) noexcept { RunGpuTests(context, RHIBackendType::Vulkan); }
}
