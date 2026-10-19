#include "LayoutBindingNumericSelfTests.h"
#include "Compiler/ShaderCompiler.h"
#include "DevelopmentShaderPaths.h"
#include "Targets/DX12ShaderTarget.h"
#include "Targets/Vulkan13ShaderTarget.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneConstantBindings.h"
#include "GGLabRuntime/Graphics/RHI/DX12/DX12ContextFactory.h"
#include "GGLabRuntime/Graphics/RHI/Vulkan/VulkanWin32ContextFactory.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/TransferManager.h"
#include "GGLabRuntime/Graphics/Asset/TextureAsset.h"
#include "GGLabRuntime/Graphics/Shader/ShaderTypes.h"
#include "GGLabTestCore/SelfTest.h"
#if GGLAB_ENABLE_VULKAN
#include "Graphics/RHI/Vulkan/VulkanContext.h"
#include "Graphics/RHI/Vulkan/Diagnostics/VulkanBackendSnapshotBuilder.h"
#include "GGLabRuntime/Diagnostics/Snapshots/VulkanBackendSnapshot.h"
#endif

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace gglab
{
	// This fixture owns its textures outside RenderGraph and explicitly restores Common
	// before transfer readback. Upload pages remain owned by the submitted frame batch.
	namespace
	{
		void Transition(RHIGraphicsCommandContext& commands, RHITextureHandle texture,
			RHIResourceState before, RHIResourceState after) noexcept
		{
			const RHITextureBarrier barrier{ texture, before, after };
			commands.TextureBarrier(std::span(&barrier, 1));
			commands.FlushBarriers();
		}

		void RunGpuTests(SelfTestContext& context, RHIBackendType backend) noexcept
		{
			const auto runtimeRoot = win32::GetExecutableDirectory();
			ShaderCompiler compiler(ResolveShaderSourceRoot(runtimeRoot), ResolveShaderCacheRoot(runtimeRoot));
			const auto compile = [&](ShaderStage stage, const wchar_t* entry)
				{
					ShaderDesc desc{ .m_SourcePath = L"Tests/SceneAtmosphereBindingsContractCompile.hlsl",
						.m_Stage = stage, .m_Entry = entry, .m_IncludeDirs = { L"." } };
					desc.m_Target = backend == RHIBackendType::DX12 ?
						MakeDX12CompileTarget(stage) :
						MakeVulkan13CompileTarget(stage);
					return compiler.Compile(desc);
				};
			const auto vertex = compile(ShaderStage::Vertex, L"VSMain");
			const auto pixel = compile(ShaderStage::Pixel, L"PSMain");
			context.Check(vertex.IsSuccess() && pixel.IsSuccess(), "Three-block sentinel shaders compile");
			if (!vertex.IsSuccess() || !pixel.IsSuccess())
			{
				return;
			}
			const auto window = CreateWindowExW(0, L"STATIC", L"GGLab binding sentinel",
				WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
			context.Check(window != nullptr, "Three-block sentinel creates a hidden host");
			if (!window)
			{
				return;
			}
			const RHIContextDesc contextDesc{ .m_Width = 64, .m_Height = 64, .m_EnableDebugValidation = true };
			std::unique_ptr<RHIContext> rhi;
			if (backend == RHIBackendType::DX12)
			{
				rhi = CreateDX12Context(contextDesc, window);
			}
#if GGLAB_ENABLE_VULKAN
			else
			{
				rhi = CreateVulkanWin32Context(contextDesc, GetModuleHandleW(nullptr), window, sizeof(void*) == 8);
			}
#endif
			context.Check(rhi != nullptr, "Three-block sentinel initializes the requested RHI");
			if (!rhi)
			{
				DestroyWindow(window);
				return;
			}
			{
				auto& device = rhi->GetDevice();
				auto& pipelines = rhi->GetPipelineSystem();
				std::printf("Binding sentinel adapter: %s\n", std::string(device.GetAdapterCompatibilityIdentity()).c_str());
				const auto composition = scene_bindings::ComposeForwardLayout(scene_bindings::GetCommonLayoutContract(), false);
				const auto layout = ResolvedRenderLayout::Create(pipelines, *composition.m_Layout);
				const auto sceneRef = ResolvedBindingRef<SceneGPU>::Resolve(layout, scene_bindings::SceneConstants);
				const auto shadowRef = ResolvedBindingRef<DirectionalShadowGPU>::Resolve(layout, scene_bindings::DirectionalShadow);
				const auto atmosphereRef = ResolvedBindingRef<AtmosphereGPU>::Resolve(layout, scene_bindings::AtmosphereConstants);
				const RHITextureDesc targetDesc{ .m_Format = RHIFormat::R32G32B32A32Float,
					.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::CopySource,
					.m_Extent = { 1, 1, 1 }, .m_DebugName = "Numeric.BindingSentinel",
					.m_ClearValue = RHIClearValue{ .m_Format = RHIFormat::R32G32B32A32Float,
						.m_Color = { -1.0f, -1.0f, -1.0f, -1.0f } } };
				RHITextureOwner target(&device, device.CreateTexture({ .m_Desc = targetDesc }));
				const RHITextureViewDesc rtvDesc{ .m_Type = RHITextureViewType::RenderTarget,
					.m_Dimension = RHITextureViewDimension::Texture2D };
				const auto rtv = target ? device.CreateTextureView(target.Get(), rtvDesc) : RHITextureViewHandle{};
				RHIGraphicsPipelineCreateInfo info{};
				info.m_Desc.m_BindingLayout = layout.GetNativeLayout();
				info.m_Desc.m_DepthStencil.m_DepthTestEnable = false;
				info.m_Desc.m_DepthStencil.m_DepthWriteEnable = false;
				info.m_Desc.m_RenderTargetFormats[0] = targetDesc.m_Format;
				info.m_Desc.m_RenderTargetCount = 1;
				const auto bytecode = [](const ShaderCompileResult& result, std::string_view entry)
					{
						const auto& artifact = result.m_Artifact;
						return ShaderBytecode{ artifact.m_Binary.Data(), artifact.m_Binary.SizeInBytes(),
							artifact.GetBinaryFormat(), ComputeShaderBinaryHash(artifact.m_Binary, artifact.GetBinaryFormat()), entry };
					};
				info.m_VertexShader = bytecode(vertex, "VSMain");
				info.m_PixelShader = bytecode(pixel, "PSMain");
				const auto pipeline = pipelines.CreateGraphicsPipeline(info);
				context.Check(target && rtv.IsValid() && pipeline.IsValid() && sceneRef.IsValid() &&
					shadowRef.IsValid() && atmosphereRef.IsValid(), "Production composition resolves all three distinct constant inputs");
				if (target && rtv.IsValid() && pipeline.IsValid())
				{
					const auto begin = rhi->BeginFrame();
					context.Check(begin.IsReady(), "Binding sentinel begins a frame");
					if (begin.IsReady())
					{
						auto& frame = *begin.GetFrame();
						auto& commands = frame.GetGraphicsContext();
						const auto uploads = frame.GetUploads();
						SceneGPU scene{};
						scene.ObjectCount = 11;
						scene.ViewBaseIndex = 1;
						scene.IBLResource.EnvironmentIntensity = 5.0f;
						DirectionalShadowGPU shadow{};
						shadow.SplitFar[0] = 22.0f;
						shadow.MainViewIndex = 1;
						shadow.DistanceFadeInvRange = 2.0f;
						auto atmosphere = MakeNeutralAtmosphere();
						atmosphere.m_Radii.m_X = 33.0f;
						atmosphere.m_World.m_W = 0.5f;
						const auto sceneSnapshot = TypedConstantSnapshot<SceneGPU>::Upload(uploads, scene);
						const auto shadowSnapshot = TypedConstantSnapshot<DirectionalShadowGPU>::Upload(uploads, shadow);
						const auto atmosphereSnapshot = TypedConstantSnapshot<AtmosphereGPU>::Upload(uploads, atmosphere);
						std::array<ViewGPU, 3> views{};
						views[0].CameraPos.m_X = -20.0f;
						views[1].CameraPos.m_X = -30.0f;
						views[2].CameraPos.m_X = 44.0f;
						const auto viewSlice = uploads.UploadStructured(std::span(views));
						context.Check(viewSlice.IsValid() && viewSlice.m_OffsetInBytes != 0,
							"View sentinel uses a nonzero upload slice offset and nonzero logical base");
						const RHIResourceState renderTarget{ RHIStage::RenderTarget, RHIAccess::RenderTarget, RHILayout::RenderTarget };
						Transition(commands, target.Get(), UndefinedRHITextureState(), renderTarget);
						const RHIRenderingAttachment attachment{ .m_View = rtv, .m_LoadOp = RHIContentLoadOp::DontCare };
						commands.BeginRendering({ .m_ColorAttachments = std::span(&attachment, 1) });
						commands.ClearColorAttachment(0, { -1.0f, -1.0f, -1.0f, -1.0f });
						commands.SetPipeline(pipeline);
						const bool bound = BindConstantSnapshot(commands, layout, sceneRef, sceneSnapshot) &&
							BindConstantSnapshot(commands, layout, shadowRef, shadowSnapshot) &&
							BindConstantSnapshot(commands, layout, atmosphereRef, atmosphereSnapshot) &&
							commands.SetReadOnlyBuffer(*layout.GetContract()->FindParameterIndex(scene_bindings::ViewTable), viewSlice);
						context.Check(bound, "Binding sentinel records three immutable snapshots and the exact View slice");
						commands.SetViewport({ 0.0f, 0.0f, 1.0f, 1.0f });
						commands.SetScissorRect({ 0, 0, 1, 1 });
						if (bound)
						{
							commands.DrawFullscreenTriangle();
						}
						commands.EndRendering();
						Transition(commands, target.Get(), renderTarget, CommonRHIResourceState());
						const auto backbuffer = frame.GetBackBuffer();
						const auto backRtv = device.CreateTextureView(backbuffer, rtvDesc);
						Transition(commands, backbuffer, UndefinedRHITextureState(), renderTarget);
						const RHIRenderingAttachment hidden{ .m_View = backRtv, .m_LoadOp = RHIContentLoadOp::DontCare };
						commands.BeginRendering({ .m_ColorAttachments = std::span(&hidden, 1) });
						commands.ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
						commands.EndRendering();
						Transition(commands, backbuffer, renderTarget, PresentRHITextureState());
						const auto end = rhi->EndFrame(frame);
						rhi->WaitIdle();
						const bool completed = end.IsCompleted() && end.HasSubmission() &&
							device.QueryFencePoint(end.GetSubmittedFence()) == RHIFenceQueryResult::Completed;
						context.Check(completed, "Sentinel submission is checked complete before reading GPU output");
						device.DestroyTextureView(backRtv);
						if (completed)
						{
							auto& transfers = rhi->GetTransferManager();
							auto batch = transfers.BeginBatch();
							auto request = batch.ReadbackTexture(target.Get(), targetDesc);
							const auto submission = batch.Submit(true);
							const bool copied = request.IsValid() && submission.m_Completion.IsValid() &&
								device.QueryFencePoint(submission.m_Completion) == RHIFenceQueryResult::Completed;
							context.Check(copied, "Sentinel readback copy is checked complete before mapping");
							if (copied)
							{
								const auto* mapped = transfers.MapTextureReadback(device, request);
								const auto data = transfers.ResolveMappedTextureReadback(request, mapped);
								if (mapped)
								{
									transfers.UnmapTextureReadback(device, request);
								}
								std::array<float, 4> value{};
								if (data.IsValid())
								{
									std::memcpy(value.data(), data.m_Pixels.data() + data.m_Subresources.front().m_DataOffset, sizeof(value));
								}
								context.Check(data.IsValid() && value == std::array{ 16.0f, 24.0f, 33.5f, 44.0f },
									"GPU reads distinct Scene b0, Shadow b3, Atmosphere b5 and offset View values simultaneously");
							}
						}
					}
				}
				rhi->WaitIdle();
				rhi->RetireCompletedWork();
				const auto uploads = rhi->GetUploadStatistics();
				context.Check(uploads.m_LastBatch.m_UploadCount == 4,
					"Native sentinel reuses the three constant uploads and one structured View upload");
				std::printf("Binding sentinel uploads=%llu, requested=%llu, reserved=%llu, pages=%llu\n",
					uploads.m_LastBatch.m_UploadCount, uploads.m_LastBatch.m_RequestedBytes,
					uploads.m_LastBatch.m_ReservedBytes, uploads.m_LastBatch.m_PageCount);
				device.DestroyTextureView(rtv);
#if GGLAB_ENABLE_VULKAN
				if (backend == RHIBackendType::Vulkan)
				{
					VulkanBackendSnapshot snapshot;
					BuildVulkanBackendSnapshot(static_cast<const VulkanContext&>(*rhi), snapshot);
					context.Check(snapshot.m_ValidationErrors == 0, "Three-block Vulkan sentinel has no validation errors");
					std::printf("Binding sentinel native layouts=%u, validation errors=%llu, warnings=%llu\n",
						snapshot.m_BindingLayoutCount, snapshot.m_ValidationErrors, snapshot.m_ValidationWarnings);
				}
#endif
			}
			rhi.reset();
			DestroyWindow(window);
		}
	}

	void RunDX12LayoutBindingNumericSelfTests(SelfTestContext& context) noexcept
	{
		RunGpuTests(context, RHIBackendType::DX12);
	}

	void RunVulkanLayoutBindingNumericSelfTests(SelfTestContext& context) noexcept
	{
		RunGpuTests(context, RHIBackendType::Vulkan);
	}
}
