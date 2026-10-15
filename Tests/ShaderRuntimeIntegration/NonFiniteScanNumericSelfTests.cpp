#include "NonFiniteScanNumericSelfTests.h"
#include "Compiler/ShaderCompiler.h"
#include "DevelopmentShaderPaths.h"
#include "Targets/DX12ShaderTarget.h"
#include "Targets/Vulkan13ShaderTarget.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchTypes.h"
#include "Graphics/Capture/FrameCaptureService.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/TransferManager.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "ShaderArtifactRuntime/NonFiniteCounterABI.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/DX12/DX12ContextFactory.h"
#include "GGLabRuntime/Graphics/RHI/Vulkan/VulkanWin32ContextFactory.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/Shader/ShaderTypes.h"
#include "GGLabTestCore/SelfTest.h"

#include <Windows.h>
#include <algorithm>
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
#include <vector>

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

		class NumericScanPrograms final : public RenderShaderProgramAccess, public RenderPipelineResolver
		{
		public:
			NumericScanPrograms(RHIPipelineSystem& pipelines, const ShaderCompileResult& clear,
				const ShaderCompileResult& texture, const ShaderCompileResult& buffer, const ShaderCompileResult* production = nullptr) : m_Pipelines(pipelines)
			{
				const std::array compiled{ &clear, &texture, &buffer, production ? production : &clear };
				for (size_t i = 0; i < compiled.size(); ++i)
				{
					const auto& artifact = compiled[i]->m_Artifact;
					m_Code[i] = { artifact.m_Binary.Data(), artifact.m_Binary.SizeInBytes(), artifact.GetBinaryFormat(),
						ComputeShaderBinaryHash(artifact.m_Binary, artifact.GetBinaryFormat()), i == 0 || i == 3 ? "CSClear" : "CSMain" };
				}
			}
			ShaderID LoadProgram(const ShaderProgramRef& ref) noexcept override
			{
				return ShaderID{ ref == shader_programs::NonFiniteCountersClearCompute ? 3u : ref == shader_programs::NonFiniteScanClearCompute ? 0u :
					ref == shader_programs::NonFiniteScanTextureCompute ? 1u : 2u };
			}
			uint64_t GetGeneration(ShaderID) const noexcept override { return 1; }
			RHIPipelineHandle Resolve(GraphicsPipelineSlot&, const GraphicsPhysicalPipelineKey&, const RenderPassInfo&) noexcept override { return {}; }
			RHIPipelineHandle Resolve(ComputePipelineSlot&, const ComputePipelineRecipe& recipe, const RenderPassInfo&) noexcept override
			{
				return m_Pipelines.CreateComputePipeline({ .m_Desc = { .m_BindingLayout = recipe.m_BindingLayout },
					.m_ComputeShader = m_Code[recipe.m_CSId.Value()] });
			}
			void GetPipelineUsages(RHIPipelineHandle, std::vector<RenderPassInfo>&) const noexcept override {}
		private:
			RHIPipelineSystem& m_Pipelines;
			std::array<ShaderBytecode, 4> m_Code;
		};

		void RunObserverGpuTests(SelfTestContext& context, RHIContext& rhi, NumericScanPrograms& programs,
			RHIBufferHandle source, uint64_t sourceBytes, const NonFiniteBufferScanLayout& layout) noexcept
		{
			auto& device = rhi.GetDevice();
			constexpr RHIResourceState sampled{ RHIStage::ComputeShader, RHIAccess::ShaderResource, RHILayout::ShaderResource };
			const std::array formats{ RHIFormat::R32G32B32A32Float, RHIFormat::R16G16Float };
			std::array<RHITextureDesc, 2> descriptions;
			std::array<RHITextureOwner, 2> textures;
			std::array<std::vector<std::vector<uint32_t>>, 2> storage;
			std::array<std::vector<uint8_t>, 2> expectedBytes;
			std::array<NonFiniteScanRecord, 2> expectedRecords;
			auto batch = rhi.GetTransferManager().BeginBatch();
			bool uploaded = true;
			for (uint32_t texture = 0; texture < textures.size(); ++texture)
			{
				auto& desc = descriptions[texture];
				desc = { .m_Format = formats[texture], .m_Usage = RHITextureUsage::Sampled | RHITextureUsage::CopySource | RHITextureUsage::CopyDest,
					.m_CreateFlags = RHITextureCreateFlags::CubeCompatible, .m_Extent = { 4, 4, 1 }, .m_ArraySize = 6, .m_MipLevels = 2 };
				textures[texture] = RHITextureOwner(&device, device.CreateTexture({ desc }));
				if (!textures[texture]) { uploaded = false; break; }
				RHITextureUploadData upload;
				storage[texture].resize(12);
				const uint32_t channels = texture == 0 ? 4 : 2;
				const uint32_t scalarBytes = texture == 0 ? 4 : 2;
				for (uint32_t slice = 0; slice < 6; ++slice)
				{
					for (uint32_t mip = 0; mip < 2; ++mip)
					{
						const uint32_t width = 4 >> mip;
						const uint32_t count = width * width * channels;
						auto& words = storage[texture][slice * 2 + mip];
						words.resize(count * scalarBytes / 4);
						for (uint32_t i = 0; i < count; ++i)
						{
							uint32_t bits = i % 2 == 0 ? 0x3f800000u : 0xbe800000u;
							uint16_t half = i % 2 == 0 ? 0x3c00u : 0xb400u;
							if (mip == 0 || slice < 2 || slice > 3 || (slice == 2 && i == 5)) { bits = 0x7fc00000u; half = 0x7e00u; }
							if (mip == 1 && slice == 3 && i == 2) { bits = 0x7f800000u; half = 0x7c00u; }
							if (mip == 1 && slice == 3 && i == 7) { bits = 0xff800000u; half = 0xfc00u; }
							std::memcpy(reinterpret_cast<uint8_t*>(words.data()) + i * scalarBytes,
								texture == 0 ? static_cast<const void*>(&bits) : static_cast<const void*>(&half), scalarBytes);
							if (mip == 1 && slice >= 2 && slice <= 3)
							{
								AccumulateNonFiniteScanValue(expectedRecords[texture], bits, (slice - 2) * count + i);
							}
						}
						if (mip == 1 && slice >= 2 && slice <= 3)
						{
							const auto* bytes = reinterpret_cast<const uint8_t*>(words.data());
							expectedBytes[texture].insert(expectedBytes[texture].end(), bytes, bytes + words.size() * 4);
						}
						upload.m_Subresources.push_back({ words.data(), uint64_t{ width } * channels * scalarBytes, uint64_t{ width } * width * channels * scalarBytes });
					}
				}
				uploaded &= batch.UploadTexture(textures[texture].Get(), upload, UndefinedRHITextureState(), sampled);
			}
			const auto uploadSubmission = batch.Submit(true); // Fixture setup only; capture never waits.
			context.Check(uploaded && uploadSubmission.m_Completion.IsValid(), "Observer fixtures upload both cube-compatible float textures and all mips/faces");
			if (!uploaded || !uploadSubmission.m_Completion.IsValid()) { return; }
			TransientResourcePool pool(&device);
			// Retain the baseline's three payloads while the observed frame reserves seven.
			FrameCaptureService service(device, { .m_MaxPayloads = 10 });
			service.ConfigureNonFiniteScans(programs, programs, rhi.GetPipelineSystem());
			const std::array<std::string, 2> names{ "Numeric.Texture32", "Numeric.Texture16" };
			std::vector<RGResourceAddress> exports{ { .m_Resource = "Numeric.Buffer" } };
			for (const auto& name : names) { exports.push_back({ .m_Resource = name, .m_Mip = 1, .m_FirstSlice = 2, .m_SliceCount = 2 }); }
			std::shared_ptr<const ResourceExportPayload> baseline;
			for (uint32_t observed = 0; observed < 2; ++observed)
			{
				const auto begin = rhi.BeginFrame();
				context.Check(begin.IsReady(), "Hidden observer-equivalence frame begins");
				if (!begin.IsReady()) { break; }
				RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
				struct Data {};
				graph.AddPass<Data>("OriginalConsumer", RGPassEncoderType::Compute, [&](auto& builder, auto&)
					{
						const auto buffer = builder.ImportBuffer("Numeric.Buffer", source,
							{ .m_SizeInBytes = sourceBytes, .m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::CopySource,
								.m_MemoryUsage = RHIMemoryUsage::CpuToGpu }, RGBufferAccess::StructuredRead, RGContentValidity::Defined);
						GGLAB_UNUSED(builder.Read(buffer, RGBufferAccess::StructuredRead));
						builder.Export(buffer, RGBufferAccess::StructuredRead);
						for (size_t i = 0; i < textures.size(); ++i)
						{
							const auto texture = builder.ImportTexture(names[i].c_str(), textures[i].Get(), descriptions[i],
								RGTextureAccess::Sample, RGContentValidity::Defined);
							GGLAB_UNUSED(builder.Read(texture, RGTextureAccess::Sample));
							builder.Export(texture, RGTextureAccess::Sample);
						}
						builder.SideEffect();
					});
				context.Check(graph.Compile(), "Original GPU observation fixture compiles independently of observers");
				GGLAB_UNUSED(service.RequestResourceExport(exports, 100 + observed, false, true));
				if (observed)
				{
					auto empty = layout; empty.m_ValidElementCount = empty.m_ScalarCount = 0;
					GGLAB_UNUSED(service.RequestNonFiniteScan({ { exports[0], layout }, { exports[0], empty }, { exports[1], {} }, { exports[2], {} } },
						71, 101, true));
				}
				const auto prepared = service.PrepareResourceExports(100 + observed, graph);
				context.Check(prepared == FrameCaptureAvailability::Available && graph.HasNonFiniteScans() == (observed != 0),
					"Opt-in scan and export observers extend one fixed GPU plan together");
				service.BeginGraphRecording(100 + observed, graph);
				RGExecuteContext execution({ .m_GraphicsCommandContext = &begin.GetFrame()->GetGraphicsContext(),
					.m_DirectComputeCommandContext = &begin.GetFrame()->GetDirectComputeContext() });
				graph.Execute(execution); service.EndGraphRecording(100 + observed, graph);
				std::vector<FrameCaptureResult> early; service.ConsumeResults(early);
				context.Check(early.empty(), "Recorded but unsubmitted observers publish no CPU result");
				auto& graphics = begin.GetFrame()->GetGraphicsContext();
				const auto backbuffer = begin.GetFrame()->GetBackBuffer();
				const auto rtv = device.CreateTextureView(backbuffer, { .m_Type = RHITextureViewType::RenderTarget, .m_Dimension = RHITextureViewDimension::Texture2D });
				const RHIResourceState renderTarget{ RHIStage::RenderTarget, RHIAccess::RenderTarget, RHILayout::RenderTarget };
				RHITextureBarrier barrier{ backbuffer, UndefinedRHITextureState(), renderTarget };
				graphics.TextureBarrier(std::span(&barrier, 1)); graphics.FlushBarriers();
				const RHIRenderingAttachment attachment{ .m_View = rtv, .m_LoadOp = RHIContentLoadOp::DontCare };
				graphics.BeginRendering({ .m_ColorAttachments = std::span(&attachment, 1) });
				graphics.ClearColorAttachment(0, { 0, 0, 0, 1 }); graphics.EndRendering();
				barrier = { backbuffer, renderTarget, PresentRHITextureState() };
				graphics.TextureBarrier(std::span(&barrier, 1)); graphics.FlushBarriers();
				const auto end = rhi.EndFrame(*begin.GetFrame());
				context.Check(end.IsCompleted() && end.HasSubmission(), "Observed GPU fixture submits its common frame fence");
				if (!end.HasSubmission()) { service.OnFrameSubmissionFailed(100 + observed); rhi.WaitIdle(); break; }
				service.OnFrameSubmitted(100 + observed, end.GetSubmittedFence());
				graph.Retire(end.GetSubmittedFence());
				rhi.WaitIdle(); // Test verification only, after normal asynchronous submission.
				std::vector<FrameCaptureResult> results; service.ConsumeResults(results);
				const auto resource = std::ranges::find(results, FrameCaptureSource::ResourceExport, &FrameCaptureResult::m_Source);
				context.Check(resource != results.end() && resource->m_Resources && resource->m_Graph && resource->m_Graph->m_Execution.m_Recorded,
					"Fenced readbacks retain imported buffers and paired execution evidence");
				if (resource != results.end() && resource->m_Resources)
				{
					const auto& arrays = resource->m_Resources->m_Arrays;
					context.Check(arrays.size() == 3 && arrays[1].m_Data == expectedBytes[0] && arrays[2].m_Data == expectedBytes[1],
						"Texture readbacks select exactly the addressed mip and cube-face range, retaining non-finite bits");
					if (!observed) { baseline = resource->m_Resources; }
					else
					{
						bool identical = baseline && baseline->m_Arrays.size() == arrays.size();
						for (size_t i = 0; identical && i < arrays.size(); ++i) { identical &= baseline->m_Arrays[i].m_Data == arrays[i].m_Data; }
						context.Check(identical, "Enabling scans leaves every exported fixture resource bit-identical");
					}
				}
				if (observed)
				{
					const auto scan = std::ranges::find(results, FrameCaptureSource::NonFiniteScan, &FrameCaptureResult::m_Source);
					context.Check(scan != results.end() && scan->m_NonFiniteScans && scan->m_NonFiniteScans->m_ConfigurationVersion == 71 && scan->m_FrameSerial == 101,
						"GPU scan payloads retain the requested configuration and submitted frame");
					if (scan != results.end() && scan->m_NonFiniteScans && scan->m_NonFiniteScans->m_Boundaries.size() == 4)
					{
						const auto& boundaries = scan->m_NonFiniteScans->m_Boundaries;
						context.Check(boundaries[0].m_Record.m_CheckedCount == layout.m_ScalarCount && boundaries[0].m_Record.m_NanCount == 2 &&
							boundaries[1].m_Result.m_Claim.m_Status == NonFiniteClaimStatus::Passed && boundaries[1].m_Record.m_CheckedCount == 0,
							"Graph buffer scans honor the float-only layout, active count and empty-range clear");
						for (size_t i = 0; i < textures.size(); ++i)
						{
							context.Check(std::memcmp(&boundaries[i + 2].m_Record, &expectedRecords[i], sizeof(NonFiniteScanRecord)) == 0,
								"Graph texture scans match the CPU reference byte-for-byte for counts, extrema and earliest scalar index");
						}
						context.Check(scan->m_NonFiniteScans->m_Production.m_Status == NonFiniteClaimStatus::Unavailable,
							"Successful GPU boundary evidence never implies sanitization/recovery instrumentation");
					}
				}
				device.DestroyTextureView(rtv);
			}
			service.Shutdown(); rhi.RetireCompletedWork();
		}

		void RunProductionGpuTests(SelfTestContext& context, RHIContext& rhi, ShaderCompiler& compiler, RHIBackendType backend) noexcept
		{
			auto& device = rhi.GetDevice();
			auto& pipelines = rhi.GetPipelineSystem();
			const uint32_t program = static_cast<uint32_t>(std::ranges::find(nonfinite::Programs, "gglab.shader.temporal-aa") - nonfinite::Programs.begin());
			auto compile = [&](std::wstring_view entry, ShaderStage stage, bool counters)
			{
				ShaderDesc desc{ .m_SourcePath = L"Tests/NonFiniteCounterFixture.hlsl", .m_Stage = stage,
					.m_Target = backend == RHIBackendType::DX12 ? MakeDX12CompileTarget(stage) : MakeVulkan13CompileTarget(stage),
					.m_Entry = std::wstring(entry), .m_IncludeDirs = { L"." } };
				desc.m_Target.m_Flags = ShaderCompileFlags::Optimization;
				if (counters)
				{
					desc.m_Defines = { { L"GGLAB_NONFINITE_COUNTERS", L"1" }, { L"GGLAB_NONFINITE_PROGRAM_INDEX", std::to_wstring(program) } };
				}
				return compiler.Compile(desc);
			};
			ShaderDesc clearDesc{ .m_SourcePath = L"Passes/PassNonFiniteCounters.hlsl", .m_Stage = ShaderStage::Compute,
				.m_Target = backend == RHIBackendType::DX12 ? MakeDX12CompileTarget(ShaderStage::Compute) : MakeVulkan13CompileTarget(ShaderStage::Compute),
				.m_Entry = L"CSClear", .m_IncludeDirs = { L"." } };
			const auto clear = compiler.Compile(clearDesc);
			const auto vs = compile(L"VSMain", ShaderStage::Vertex, false);
			context.Check(clear.IsSuccess() && vs.IsSuccess(), "Production counter clear and fullscreen fixture compile");
			if (!clear.IsSuccess() || !vs.IsSuccess()) { return; }
			NumericScanPrograms programs(pipelines, clear, clear, clear, &clear);
			const auto bytecode = [](const ShaderCompileResult& code, const char* entry)
			{
				const auto& artifact = code.m_Artifact;
				return ShaderBytecode{ artifact.m_Binary.Data(), artifact.m_Binary.SizeInBytes(), artifact.GetBinaryFormat(),
					ComputeShaderBinaryHash(artifact.m_Binary, artifact.GetBinaryFormat()), entry };
			};
			const std::array input{ 0x7fc00001u, 0x7f800000u, std::bit_cast<uint32_t>(-0.25f), std::bit_cast<uint32_t>(70000.0f),
				0xff800000u, 0u, 0u, 0u };
			RHIBufferOwner source(&device, device.CreateBuffer({ .m_SizeInBytes = sizeof(input), .m_StrideInBytes = 4,
				.m_Usage = RHIBufferUsage::Structured, .m_MemoryUsage = RHIMemoryUsage::CpuToGpu }));
			if (!source) { context.Check(false, "Counter input allocates"); return; }
			void* mapped = device.MapBuffer(source.Get(), {});
			if (!mapped) { context.Check(false, "Counter input maps"); return; }
			std::memcpy(mapped, input.data(), sizeof(input)); device.UnmapBuffer(source.Get(), { 0, sizeof(input) });
			TransientResourcePool pool(&device);
			std::shared_ptr<const ResourceExportPayload> baseline;
			for (uint32_t enabled = 0; enabled < 2; ++enabled)
			{
				const auto cs = compile(L"CSMain", ShaderStage::Compute, enabled != 0);
				const auto ps = compile(L"PSMain", ShaderStage::Pixel, enabled != 0);
				context.Check(cs.IsSuccess() && ps.IsSuccess(), "Optimized compute and pixel counter variants compile");
				if (!cs.IsSuccess() || !ps.IsSuccess()) { return; }
				RHIBindingLayoutDesc computeLayout;
				computeLayout.m_Slots[0] = { .m_Type = RHIBindingType::ReadOnlyStorageBuffer, .m_Visibility = RHIShaderStage::All, .m_Binding = 0 };
				computeLayout.m_Slots[1] = { .m_Type = RHIBindingType::ReadWriteStorageBuffer, .m_Visibility = RHIShaderStage::Compute, .m_Binding = 0 };
				computeLayout.m_SlotCount = 2;
				RHIBindingLayoutDesc graphicsLayout;
				graphicsLayout.m_Slots[0] = computeLayout.m_Slots[0]; graphicsLayout.m_SlotCount = 1;
				for (const auto type : { RHIBindingType::BindlessResourceTable, RHIBindingType::BindlessSamplerTable })
				{
					const RHIBindingSlotDesc heap{ .m_Type = type, .m_Visibility = RHIShaderStage::All, .m_Count = 0 };
					computeLayout.m_Slots[computeLayout.m_SlotCount++] = heap;
					graphicsLayout.m_Slots[graphicsLayout.m_SlotCount++] = heap;
				}
				if (enabled)
				{
					const RHIBindingSlotDesc slot{ .m_Type = RHIBindingType::ReadWriteStorageBuffer, .m_Visibility = RHIShaderStage::All, .m_Binding = nonfinite::CounterBinding };
					computeLayout.m_Slots[computeLayout.m_SlotCount++] = slot;
					graphicsLayout.m_Slots[graphicsLayout.m_SlotCount++] = slot;
				}
				const auto compute = pipelines.CreateComputePipeline({ .m_Desc = { .m_BindingLayout = pipelines.CreateBindingLayout(computeLayout) }, .m_ComputeShader = bytecode(cs, "CSMain") });
				RHIGraphicsPipelineCreateInfo graphicsInfo;
				graphicsInfo.m_Desc.m_BindingLayout = pipelines.CreateBindingLayout(graphicsLayout);
				graphicsInfo.m_Desc.m_RenderTargetCount = 1; graphicsInfo.m_Desc.m_RenderTargetFormats[0] = RHIFormat::R32G32B32A32Float;
				graphicsInfo.m_Desc.m_Rasterizer.m_CullMode = RHICullMode::None;
				graphicsInfo.m_Desc.m_DepthStencil.m_DepthTestEnable = false;
				graphicsInfo.m_Desc.m_DepthStencil.m_DepthWriteEnable = false;
				graphicsInfo.m_VertexShader = bytecode(vs, "VSMain"); graphicsInfo.m_PixelShader = bytecode(ps, "PSMain");
				const auto graphics = pipelines.CreateGraphicsPipeline(graphicsInfo);
				context.Check(compute.IsValid() && graphics.IsValid(), "Pixel and compute storage layouts create real pipelines");
				if (!compute.IsValid() || !graphics.IsValid()) { return; }
				FrameCaptureService service(device);
				service.ConfigureNonFiniteScans(programs, programs, pipelines);
				if (enabled) { service.EnableNonFiniteProduction(); }
				const auto begin = rhi.BeginFrame(); if (!begin.IsReady()) { context.Check(false, "Counter frame begins"); return; }
				RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
				struct Data { RGBufferId m_Input; RGBufferId m_Output; RGTextureViewId m_View; };
				RGBufferId inputId;
				graph.AddPass<Data>("Fixture.SanitizingWrite", RGPassEncoderType::Compute, [&](auto& builder, auto& data)
					{
						inputId = builder.ImportBuffer("Fixture.Input", source.Get(), { .m_SizeInBytes = sizeof(input), .m_StrideInBytes = 4,
							.m_Usage = RHIBufferUsage::Structured, .m_MemoryUsage = RHIMemoryUsage::CpuToGpu }, RGBufferAccess::StructuredRead, RGContentValidity::Defined);
						data.m_Input = builder.Read(inputId, RGBufferAccess::StructuredRead);
						const auto output = builder.CreateBuffer("Fixture.Output", { .m_SizeInBytes = 32, .m_StrideInBytes = 4 });
						data.m_Output = builder.Write(output, RGBufferAccess::StorageWrite); builder.SideEffect();
					}, [&](RGExecuteContext& execution, Data& data)
					{
						auto* commands = execution.GetDirectComputeCommandContext(); commands->SetPipeline(compute);
						commands->SetReadOnlyBuffer(0, execution.GetBufferHandle(data.m_Input)); commands->SetReadWriteBuffer(1, execution.GetBufferHandle(data.m_Output)); commands->Dispatch(1, 1, 1);
					});
				graph.AddPass<Data>("Fixture.PixelSanitization", RGPassEncoderType::Graphics, [&](auto& builder, auto& data)
					{
						data.m_Input = builder.Read(inputId, RGBufferAccess::StructuredRead);
						const auto image = builder.CreateTexture("Fixture.Image", { .m_Format = RHIFormat::R32G32B32A32Float, .m_Extent = { 2, 2, 1 } });
						const auto written = builder.Write(image, RGTextureAccess::RenderTarget); data.m_View = builder.template CreateView<RHITextureViewType::RenderTarget>(written);
						builder.SideEffect();
					}, [&](RGExecuteContext& execution, Data& data)
					{
						auto* commands = execution.GetGraphicsCommandContext(); commands->SetPipeline(graphics); commands->SetReadOnlyBuffer(0, execution.GetBufferHandle(data.m_Input));
						const RHIRenderingAttachment attachment{ .m_View = execution.GetViewHandle(data.m_View), .m_LoadOp = RHIContentLoadOp::DontCare };
						commands->BeginRendering({ .m_ColorAttachments = std::span(&attachment, 1) }); commands->SetViewport({ 0, 0, 2, 2, 0, 1 }); commands->SetScissorRect({ 0, 0, 2, 2 }); commands->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList); commands->Draw(3); commands->EndRendering();
					});
				graph.AddPass<Data>("Fixture.Culled", RGPassEncoderType::Compute, [](auto& builder, auto& data)
					{
						const auto unused = builder.CreateBuffer("Fixture.Unused", { .m_SizeInBytes = 4 });
						data.m_Output = builder.Write(unused, RGBufferAccess::StorageWrite);
					});
				context.Check(graph.Compile(), "Counter fixture freezes ordinary graph liveness first");
				GGLAB_UNUSED(service.RequestResourceExport({ { .m_Resource = "Fixture.Output" }, { .m_Resource = "Fixture.Image" } }, 501 + enabled, false, true));
				context.Check(service.PrepareResourceExports(501 + enabled, graph) == FrameCaptureAvailability::Available, "Production and resource readbacks share one graph extension");
				RGSnapshot snapshot; BuildRenderGraphSnapshot(graph, snapshot);
				context.Check(std::ranges::any_of(snapshot.m_Passes, [](const auto& pass) { return pass.m_Name == "Fixture.Culled" && pass.m_Culled; }),
					"Production counter writes do not revive a culled original producer");
				service.BeginGraphRecording(501 + enabled, graph);
				RGExecuteContext execution({ &begin.GetFrame()->GetGraphicsContext(), &begin.GetFrame()->GetDirectComputeContext() });
				graph.Execute(execution); service.EndGraphRecording(501 + enabled, graph);
				service.CollectCompleted(); context.Check(!service.GetLatestNonFiniteProductionFrame(), "Unsubmitted production counters publish no evidence");
				auto& commands = begin.GetFrame()->GetGraphicsContext(); const auto back = begin.GetFrame()->GetBackBuffer();
				const auto rtv = device.CreateTextureView(back, { .m_Type = RHITextureViewType::RenderTarget, .m_Dimension = RHITextureViewDimension::Texture2D });
				const RHIResourceState target{ RHIStage::RenderTarget, RHIAccess::RenderTarget, RHILayout::RenderTarget };
				RHITextureBarrier barrier{ back, UndefinedRHITextureState(), target }; commands.TextureBarrier(std::span(&barrier, 1)); commands.FlushBarriers();
				const RHIRenderingAttachment attachment{ .m_View = rtv, .m_LoadOp = RHIContentLoadOp::DontCare };
				commands.BeginRendering({ .m_ColorAttachments = std::span(&attachment, 1) }); commands.ClearColorAttachment(0, { 0, 0, 0, 1 }); commands.EndRendering();
				barrier = { back, target, PresentRHITextureState() }; commands.TextureBarrier(std::span(&barrier, 1)); commands.FlushBarriers();
				const auto end = rhi.EndFrame(*begin.GetFrame());
				if (!end.HasSubmission()) { service.OnFrameSubmissionFailed(501 + enabled); rhi.WaitIdle(); context.Check(false, "Counter frame submits"); return; }
				service.OnFrameSubmitted(501 + enabled, end.GetSubmittedFence()); graph.Retire(end.GetSubmittedFence()); rhi.WaitIdle();
				device.DestroyTextureView(rtv);
				std::vector<FrameCaptureResult> results; service.ConsumeResults(results);
				const auto exported = std::ranges::find(results, FrameCaptureSource::ResourceExport, &FrameCaptureResult::m_Source);
				context.Check(exported != results.end() && exported->m_Resources && exported->m_Resources->m_Arrays.size() == 2, "Counter fixture exports both compute values and pixel image");
				if (exported == results.end() || !exported->m_Resources) { return; }
				if (!enabled) { baseline = exported->m_Resources; }
				else
				{
					context.Check(baseline && baseline->m_Arrays[0].m_Data == exported->m_Resources->m_Arrays[0].m_Data && baseline->m_Arrays[1].m_Data == exported->m_Resources->m_Arrays[1].m_Data,
						"Production counter variants preserve compute exports and pixel image bit-for-bit");
					const auto production = service.GetLatestNonFiniteProductionFrame();
					context.Check(production && production->m_FrameSerial == 502 && production->m_Claim.m_Status == NonFiniteClaimStatus::Failed,
						"A normal sanitized image still fails production coverage at its completed frame");
					if (production)
					{
						const auto find = [&](std::string_view id) { return std::ranges::find_if(production->m_Sites, [&](const auto& site) { return site.m_ProgramIndex == program && nonfinite::Sites[site.m_SiteIndex].m_Id == id; }); };
						const auto output = find("pass-temporal-aa.csmain.sanitize-hdrcolor"); const auto history = find("pass-temporal-aa.csmain.is-temporal-color-finite");
						context.Check(output != production->m_Sites.end() && output->m_Counters.m_NonFiniteReplacements == 10 && output->m_Counters.m_NegativeClamps == 5 && output->m_Counters.m_Fp16MaximumClamps == 1,
							"Compute and pixel counters retain exact scalar replacements and finite clamp counts");
						context.Check(history != production->m_Sites.end() && history->m_Counters.m_NonFiniteReplacements == 1,
							"A non-finite history recovered before sanitization still records its recovery site");
					}
				}
				if (enabled)
				{
					results.clear();
					context.Check(service.GetResourceExportFrameAvailability() == FrameCaptureAvailability::Available,
						"Retained production evidence never permanently blocks scheduled export admission");
				}
				service.Shutdown();
			}
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
			shader.m_Defines.clear();
			const auto textureScan = compiler.Compile(shader);
			context.Check(clear.IsSuccess() && scan.IsSuccess() && textureScan.IsSuccess(), "Production scan and clear shaders compile for the requested backend");
			if (!clear.IsSuccess() || !scan.IsSuccess() || !textureScan.IsSuccess())
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
			RunProductionGpuTests(context, *rhi, compiler, backend);
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
						.m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::CopySource, .m_MemoryUsage = RHIMemoryUsage::CpuToGpu, .m_DebugName = "Numeric.ScanInput" }));
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
			NumericScanPrograms programs(pipelines, clear, textureScan, scan);
			RunObserverGpuTests(context, *rhi, programs, source.Get(), sizeof(words), layout);
		}
	}

	void RunDX12NonFiniteScanNumericSelfTests(SelfTestContext& context) noexcept { RunGpuTests(context, RHIBackendType::DX12); }
	void RunVulkanNonFiniteScanNumericSelfTests(SelfTestContext& context) noexcept { RunGpuTests(context, RHIBackendType::Vulkan); }
}
