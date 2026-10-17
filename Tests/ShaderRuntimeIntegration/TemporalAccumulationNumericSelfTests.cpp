#include "TemporalAccumulationNumericSelfTests.h"
#include "Compiler/ShaderCompiler.h"
#include "DevelopmentShaderPaths.h"
#include "Targets/DX12ShaderTarget.h"
#include "Targets/Vulkan13ShaderTarget.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "GGLabRuntime/Graphics/Asset/TextureAsset.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/DX12/DX12ContextFactory.h"
#include "GGLabRuntime/Graphics/RHI/Vulkan/VulkanWin32ContextFactory.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/Shader/ShaderTypes.h"
#include "GGLabRuntime/Graphics/TransferManager.h"
#include "GGLabTestCore/SelfTest.h"

#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace gglab
{
	namespace
	{
		class HiddenTestWindow
		{
		public:
			HiddenTestWindow() noexcept : m_Window(CreateWindowExW(0, L"STATIC",
				L"GGLab temporal accumulation numeric test", WS_POPUP, 0, 0, 64, 64,
				nullptr, nullptr, GetModuleHandleW(nullptr), nullptr))
			{
			}
			HiddenTestWindow(const HiddenTestWindow&) = delete;
			HiddenTestWindow& operator=(const HiddenTestWindow&) = delete;
			~HiddenTestWindow()
			{
				if (m_Window)
				{
					DestroyWindow(m_Window);
				}
			}
			[[nodiscard]] HWND Get() const noexcept { return m_Window; }
		private:
			HWND m_Window = nullptr;
		};

		constexpr std::array<std::string_view, 11> ProbeNames{
			"Zero feedback", "Fractional capacity", "Unit sample matches Native",
			"Low-feedback weighted goldens", "Valid low-capacity storage",
			"Rejected and invalid history", "Monotonic feedback", "Packed low feedback",
			"Default compatibility", "Feedback capacity transition", "Invalid feedback",
		};

		void Transition(RHIGraphicsCommandContext& commands, RHITextureHandle texture,
			RHIResourceState before, RHIResourceState after) noexcept
		{
			// Test-owned targets are outside RenderGraph; this fixture owns every state
			// transition and waits for graphics completion before readback or destruction.
			const RHITextureBarrier barrier{ texture, before, after };
			commands.TextureBarrier(std::span(&barrier, 1));
			commands.FlushBarriers();
		}

		void ValidateReadback(SelfTestContext& context, RHIContext& rhi,
			RHITextureHandle texture, const RHITextureDesc& desc) noexcept
		{
			auto& transfers = rhi.GetTransferManager();
			auto batch = transfers.BeginBatch();
			auto request = batch.ReadbackTexture(texture, desc);
			const auto submission = batch.Submit(true);
			const bool completed = request.IsValid() && submission.m_Completion.IsValid() &&
				rhi.GetDevice().IsFencePointCompleted(submission.m_Completion);
			context.Check(completed, "Temporal numeric readback completes before mapping");
			if (!completed)
			{
				return;
			}
			const std::byte* mapped = transfers.MapTextureReadback(rhi.GetDevice(), request);
			const TextureAssetData data = transfers.ResolveMappedTextureReadback(request, mapped);
			if (mapped)
			{
				transfers.UnmapTextureReadback(rhi.GetDevice(), request);
			}
			const bool valid = data.IsValid() && data.m_ResourceFormat == RHIFormat::R32G32B32A32Float &&
				data.m_Extent.m_Width == ProbeNames.size() && data.m_Extent.m_Height == 1;
			context.Check(valid, "Temporal numeric readback owns a complete RGBA32F probe row");
			if (!valid)
			{
				return;
			}
			const auto& subresource = data.m_Subresources.front();
			for (size_t index = 0; index < ProbeNames.size(); ++index)
			{
				std::array<float, 4> result{};
				std::memcpy(result.data(), data.m_Pixels.data() + subresource.m_DataOffset +
					index * sizeof(result), sizeof(result));
				context.Check(result == std::array<float, 4>{}, std::format(
					"Production temporal HLSL: {} ({}, {}, {}, {})",
					ProbeNames[index], result[0], result[1], result[2], result[3]));
			}
		}

		void RunGpuTests(SelfTestContext& context, RHIBackendType backend) noexcept
		{
			const auto runtimeRoot = win32::GetExecutableDirectory();
			ShaderCompiler compiler(ResolveShaderSourceRoot(runtimeRoot), ResolveShaderCacheRoot(runtimeRoot));
			const auto compile = [&](ShaderStage stage, const wchar_t* entry)
				{
					ShaderDesc desc{ .m_SourcePath = L"Tests/TemporalHistoryAccumulationContractCompile.hlsl",
						.m_Stage = stage, .m_Entry = entry, .m_IncludeDirs = { L"." } };
					desc.m_Target = backend == RHIBackendType::DX12
						? MakeDX12CompileTarget(stage) : MakeVulkan13CompileTarget(stage);
					return compiler.Compile(desc);
				};
			const auto vertex = compile(ShaderStage::Vertex, L"VSMain");
			const auto pixel = compile(ShaderStage::Pixel, L"PSMain");
			context.Check(vertex.IsSuccess() && pixel.IsSuccess(), "Temporal numeric probe shaders compile");
			if (!vertex.IsSuccess() || !pixel.IsSuccess())
			{
				return;
			}
			HiddenTestWindow window;
			context.Check(window.Get() != nullptr, "Temporal numeric host creates a hidden window");
			if (!window.Get())
			{
				return;
			}
			const RHIContextDesc contextDesc{ .m_Width = 64, .m_Height = 64, .m_EnableDebugValidation = true };
			std::unique_ptr<RHIContext> rhi;
			if (backend == RHIBackendType::DX12)
			{
				rhi = CreateDX12Context(contextDesc, window.Get());
			}
#if GGLAB_ENABLE_VULKAN
			else
			{
				rhi = CreateVulkanWin32Context(contextDesc, GetModuleHandleW(nullptr), window.Get(), sizeof(void*) == 8);
			}
#endif
			context.Check(rhi != nullptr, "Temporal numeric RHI initializes without backend fallback");
			if (!rhi)
			{
				return;
			}
			std::printf("Temporal numeric adapter identity: %s\n",
				std::string(rhi->GetDevice().GetAdapterCompatibilityIdentity()).c_str());
			auto& device = rhi->GetDevice();
			auto& pipelines = rhi->GetPipelineSystem();
			const RHITextureDesc targetDesc{ .m_Format = RHIFormat::R32G32B32A32Float,
				.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::CopySource,
				.m_Extent = { static_cast<uint32_t>(ProbeNames.size()), 1, 1 },
				.m_DebugName = "Numeric.TemporalAccumulation",
				.m_ClearValue = RHIClearValue{ .m_Format = RHIFormat::R32G32B32A32Float,
					.m_Color = { 1.0f, 1.0f, 1.0f, 1.0f } } };
			RHITextureOwner target(&device, device.CreateTexture({ .m_Desc = targetDesc }));
			const RHITextureViewDesc rtvDesc{ .m_Type = RHITextureViewType::RenderTarget,
				.m_Dimension = RHITextureViewDimension::Texture2D };
			const auto targetRtv = target ? device.CreateTextureView(target.Get(), rtvDesc) : RHITextureViewHandle{};
			const auto layout = pipelines.CreateBindingLayout({});
			RHIGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.m_Desc.m_BindingLayout = layout;
			pipelineInfo.m_Desc.m_DepthStencil.m_DepthTestEnable = false;
			pipelineInfo.m_Desc.m_DepthStencil.m_DepthWriteEnable = false;
			pipelineInfo.m_Desc.m_RenderTargetFormats[0] = targetDesc.m_Format;
			pipelineInfo.m_Desc.m_RenderTargetCount = 1;
			const auto bytecode = [](const ShaderCompileResult& result, std::string_view entry)
				{
					const auto& artifact = result.m_Artifact;
					return ShaderBytecode{ artifact.m_Binary.Data(), artifact.m_Binary.SizeInBytes(),
						artifact.GetBinaryFormat(), ComputeShaderBinaryHash(artifact.m_Binary, artifact.GetBinaryFormat()), entry };
				};
			pipelineInfo.m_VertexShader = bytecode(vertex, "VSMain");
			pipelineInfo.m_PixelShader = bytecode(pixel, "PSMain");
			const auto pipeline = layout.IsValid() ? pipelines.CreateGraphicsPipeline(pipelineInfo) : RHIPipelineHandle{};
			context.Check(target && targetRtv.IsValid() && pipeline.IsValid(), "Temporal numeric target and pipeline are ready");
			if (target && targetRtv.IsValid() && pipeline.IsValid())
			{
				const auto begin = rhi->BeginFrame();
				context.Check(begin.IsReady(), "Temporal numeric hidden frame begins");
				if (begin.IsReady())
				{
					auto& commands = begin.GetFrame()->GetGraphicsContext();
					const RHIResourceState renderTarget{ RHIStage::RenderTarget, RHIAccess::RenderTarget, RHILayout::RenderTarget };
					Transition(commands, target.Get(), UndefinedRHITextureState(), renderTarget);
					const RHIRenderingAttachment attachment{ .m_View = targetRtv, .m_LoadOp = RHIContentLoadOp::DontCare };
					commands.BeginRendering({ .m_ColorAttachments = std::span(&attachment, 1) });
					// A failed draw leaves a nonzero sentinel rather than accidentally passing.
					commands.ClearColorAttachment(0, { 1.0f, 1.0f, 1.0f, 1.0f });
					commands.SetPipeline(pipeline);
					commands.SetViewport({ 0.0f, 0.0f, static_cast<float>(ProbeNames.size()), 1.0f });
					commands.SetScissorRect({ 0, 0, static_cast<int32_t>(ProbeNames.size()), 1 });
					commands.DrawFullscreenTriangle();
					commands.EndRendering();
					// TransferBatch borrows textures in Common and restores that state.
					Transition(commands, target.Get(), renderTarget, CommonRHIResourceState());
					const auto backbuffer = begin.GetFrame()->GetBackBuffer();
					const auto backbufferRtv = device.CreateTextureView(backbuffer, rtvDesc);
					Transition(commands, backbuffer, UndefinedRHITextureState(), renderTarget);
					const RHIRenderingAttachment hiddenAttachment{ .m_View = backbufferRtv, .m_LoadOp = RHIContentLoadOp::DontCare };
					commands.BeginRendering({ .m_ColorAttachments = std::span(&hiddenAttachment, 1) });
					commands.ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
					commands.EndRendering();
					Transition(commands, backbuffer, renderTarget, PresentRHITextureState());
					const auto end = rhi->EndFrame(*begin.GetFrame());
					rhi->WaitIdle();
					const bool completed = end.IsCompleted() && end.HasSubmission() &&
						device.IsFencePointCompleted(end.GetSubmittedFence());
					context.Check(completed, "Temporal probe draw completes before transfer and destruction");
					device.DestroyTextureView(backbufferRtv);
					if (completed)
					{
						ValidateReadback(context, *rhi, target.Get(), targetDesc);
					}
				}
			}
			rhi->WaitIdle();
			rhi->RetireCompletedWork();
			device.DestroyTextureView(targetRtv);
		}
	}

	void RunDX12TemporalAccumulationNumericSelfTests(SelfTestContext& context) noexcept
	{
		RunGpuTests(context, RHIBackendType::DX12);
	}

	void RunVulkanTemporalAccumulationNumericSelfTests(SelfTestContext& context) noexcept
	{
		RunGpuTests(context, RHIBackendType::Vulkan);
	}
}
