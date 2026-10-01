#include "BrdfLutNumericSelfTests.h"
#include "Compiler/ShaderCompiler.h"
#include "DevelopmentShaderPaths.h"
#include "Targets/DX12ShaderTarget.h"
#include "Targets/Vulkan13ShaderTarget.h"
#include "GGLabFoundation/Platform/Win/Win32PathUtils.h"
#include "GGLabRuntime/Graphics/IBLBakeConfig.h"
#include "GGLabRuntime/Graphics/RHI/DX12/DX12ContextFactory.h"
#include "GGLabRuntime/Graphics/RHI/Vulkan/VulkanWin32ContextFactory.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/TransferManager.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#if GGLAB_ENABLE_VULKAN
#include "Graphics/RHI/Vulkan/VulkanContext.h"
#include "Graphics/RHI/Vulkan/Diagnostics/VulkanBackendSnapshotBuilder.h"
#include "GGLabRuntime/Diagnostics/Snapshots/VulkanBackendSnapshot.h"
#endif
#include "GGLabTestCore/SelfTest.h"

#include <d3d12sdklayers.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	namespace
	{
		struct BrdfIntegral
		{
			double m_A = 0.0;
			double m_B = 0.0;
		};

		struct ProbeParameters
		{
			uint32_t m_TextureIndex;
			uint32_t m_SamplerIndex;
			uint32_t m_Padding[2]{};
		};
		static_assert(sizeof(ProbeParameters) == 16);
		static_assert(offsetof(ProbeParameters, m_TextureIndex) == 0);
		static_assert(offsetof(ProbeParameters, m_SamplerIndex) == 4);
		static_assert(offsetof(ProbeParameters, m_Padding) == 8);

		constexpr double Pi = std::numbers::pi_v<double>;
		constexpr double ReferenceConvergenceTolerance = 0.0005;
		// The production estimator uses 1024 NDF samples. Allow one percentage
		// point of absolute A/B error, three near grazing where NDF sampling has
		// greater variance. FP16 quantization is covered by these budgets. The
		// independent integral must converge much more tightly than the LUT gate.
		constexpr double LutCoefficientTolerance = 0.01;
		constexpr double GrazingCoefficientTolerance = 0.03;
		constexpr double FurnaceTolerance = 0.035;
		constexpr std::array ProbeF0{ 0.04, 0.08, 0.7, 1.0 };

		// Independent double-precision quadrature, with no Hammersley sequence or
		// shared HLSL implementation. Let u be the analytic GGX NDF CDF, so
		// tan(thetaH)^2 = alpha^2*u/(1-u), and D(H)*NoH*dOmegaH = du*dPhi/(2*pi).
		// The L=reflect(-V,H) Jacobian gives G*VoH/(NoV*NoH). Integrate only
		// the analytic azimuth interval where NoL>0, using midpoint grids. This
		// avoids undersampling smooth GGX peaks with a uniform light grid. Use
		// u=1-t^2, du=2*t*dt to remove the grazing endpoint singularity at u=1.
		[[nodiscard]] BrdfIntegral IntegrateReference(
			double noV, double roughness, uint32_t radialSamples, uint32_t azimuthSamples) noexcept
		{
			const double alphaSquared = std::pow(roughness, 4.0);
			const double viewX = std::sqrt(1.0 - noV * noV);
			const double viewMask = std::sqrt(1.0 + alphaSquared * (1.0 / (noV * noV) - 1.0));
			BrdfIntegral result{};
			for (uint32_t radial = 0; radial < radialSamples; ++radial)
			{
				const double t = (radial + 0.5) / radialSamples;
				const double u = 1.0 - t * t;
				const double noH = t / std::sqrt(t * t + alphaSquared * u);
				const double halfX = std::sqrt(1.0 - noH * noH);
				const double lightBase = noV * (2.0 * noH * noH - 1.0);
				const double lightAmplitude = 2.0 * viewX * halfX * noH;
				double visibleAngle = Pi;
				if (lightAmplitude > 0.0)
				{
					visibleAngle = std::acos(std::clamp(-lightBase / lightAmplitude, -1.0, 1.0));
				}
				else if (lightBase <= 0.0)
				{
					continue;
				}
				if (visibleAngle == 0.0)
				{
					continue;
				}
				for (uint32_t azimuth = 0; azimuth < azimuthSamples; ++azimuth)
				{
					const double phi = (azimuth + 0.5) * visibleAngle / azimuthSamples;
					const double cosPhi = std::cos(phi);
					const double voH = viewX * halfX * cosPhi + noV * noH;
					const double noL = lightBase + lightAmplitude * cosPhi;
					const double lightMask = std::sqrt(1.0 + alphaSquared * (1.0 / (noL * noL) - 1.0));
					const double masking = 2.0 / (viewMask + lightMask);
					const double weight = masking * voH / (noV * noH) * visibleAngle / Pi * (2.0 * t);
					const double fresnelEdge = std::pow(1.0 - voH, 5.0);
					result.m_A += (1.0 - fresnelEdge) * weight;
					result.m_B += fresnelEdge * weight;
				}
			}
			result.m_A /= static_cast<double>(radialSamples) * azimuthSamples;
			result.m_B /= static_cast<double>(radialSamples) * azimuthSamples;
			return result;
		}

		[[nodiscard]] double CoefficientError(BrdfIntegral actual, BrdfIntegral expected) noexcept
		{
			return std::max(std::abs(actual.m_A - expected.m_A), std::abs(actual.m_B - expected.m_B));
		}

		[[nodiscard]] BrdfIntegral ConvergedReference(
			SelfTestContext& context, double noV, double roughness) noexcept
		{
			const BrdfIntegral coarse = IntegrateReference(noV, roughness, 512, 128);
			const BrdfIntegral fine = IntegrateReference(noV, roughness, 1024, 256);
			context.Check(CoefficientError(coarse, fine) < ReferenceConvergenceTolerance,
				std::format("Reference converges at NoV={:.6f}, roughness={:.6f}, delta={:.6f}",
					noV, roughness, CoefficientError(coarse, fine)));
			return fine;
		}

		class HiddenTestWindow
		{
		public:
			HiddenTestWindow() noexcept : m_Window(CreateWindowExW(0, L"STATIC",
				L"GGLab BRDF LUT numeric test", WS_POPUP, 0, 0, 64, 64,
				nullptr, nullptr, GetModuleHandleW(nullptr), nullptr))
			{
			}
			GGLAB_DELETE_COPYABLE_MOVABLE(HiddenTestWindow);
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

		constexpr RHIResourceState RenderTargetState{
			RHIStage::RenderTarget, RHIAccess::RenderTarget, RHILayout::RenderTarget };
		constexpr RHIResourceState SampleState{
			RHIStage::PixelShader, RHIAccess::ShaderResource, RHILayout::ShaderResource };

		[[nodiscard]] ShaderBytecode Bytecode(const ShaderCompileResult& result, std::string_view entry) noexcept
		{
			const auto& artifact = result.m_Artifact;
			return { artifact.m_Binary.Data(), artifact.m_Binary.SizeInBytes(), artifact.GetBinaryFormat(),
				ComputeShaderBinaryHash(artifact.m_Binary, artifact.GetBinaryFormat()), entry };
		}

		[[nodiscard]] RHIPipelineHandle CreatePipeline(RHIPipelineSystem& pipelines,
			RHIBindingLayoutHandle layout, const ShaderCompileResult& vertex,
			const ShaderCompileResult& pixel, RHIFormat format, std::string_view entry) noexcept
		{
			RHIGraphicsPipelineCreateInfo info{};
			info.m_Desc.m_BindingLayout = layout;
			// Match the production BRDF LUT pass: fullscreen triangle, default
			// rasterizer/blend, depth disabled and one single-sampled target.
			info.m_Desc.m_DepthStencil.m_DepthTestEnable = false;
			info.m_Desc.m_DepthStencil.m_DepthWriteEnable = false;
			info.m_Desc.m_RenderTargetFormats[0] = format;
			info.m_Desc.m_RenderTargetCount = 1;
			info.m_VertexShader = Bytecode(vertex, "VSMain");
			info.m_PixelShader = Bytecode(pixel, entry);
			return pipelines.CreateGraphicsPipeline(info);
		}

		void Transition(RHIGraphicsCommandContext& commands, RHITextureHandle texture,
			RHIResourceState before, RHIResourceState after) noexcept
		{
			// These test-owned RHI textures are outside RenderGraph. The harness
			// owns their complete state path, with graphics idle before transfer.
			const RHITextureBarrier barrier{ texture, before, after };
			commands.TextureBarrier(std::span(&barrier, 1));
			commands.FlushBarriers();
		}

		void Draw(RHIGraphicsCommandContext& commands, RHITextureViewHandle target,
			RHIPipelineHandle pipeline, uint32_t width, uint32_t height,
			const ProbeParameters* parameters = nullptr) noexcept
		{
			const RHIRenderingAttachment attachment{ .m_View = target, .m_LoadOp = RHIContentLoadOp::DontCare };
			commands.BeginRendering({ .m_ColorAttachments = std::span(&attachment, 1) });
			commands.SetPipeline(pipeline);
			if (parameters)
			{
				commands.SetPushConstants(0, *parameters);
			}
			commands.SetViewport({ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height) });
			commands.SetScissorRect({ 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height) });
			commands.DrawFullscreenTriangle();
			commands.EndRendering();
		}

		[[nodiscard]] TextureAssetData Readback(SelfTestContext& context, RHIContext& rhi,
			RHITextureHandle texture, const RHITextureDesc& desc) noexcept
		{
			auto& transfers = rhi.GetTransferManager();
			auto batch = transfers.BeginBatch();
			auto request = batch.ReadbackTexture(texture, desc);
			const RHITransferSubmission submission = batch.Submit(true);
			auto& device = rhi.GetDevice();
			const bool completed = request.IsValid() && submission.m_Completion.IsValid() &&
				device.IsFencePointCompleted(submission.m_Completion);
			context.Check(completed, "Texture readback fence completes before mapping");
			if (!completed)
			{
				return {};
			}
			const std::byte* mapped = transfers.MapTextureReadback(device, request);
			// Resolve copies pitched native rows to owned tight storage. The request
			// and mapping remain alive through the copy, including on error paths.
			TextureAssetData data = transfers.ResolveMappedTextureReadback(request, mapped);
			if (mapped)
			{
				transfers.UnmapTextureReadback(device, request);
			}
			context.Check(data.IsValid(), "Readback resolves a valid owned texture payload");
			return data;
		}

		[[nodiscard]] double ReadChannel(const TextureAssetData& data,
			uint32_t x, uint32_t y, uint32_t channel) noexcept
		{
			const auto& subresource = data.m_Subresources.front();
			const uint32_t channelBytes = data.m_ResourceFormat == RHIFormat::R16G16Float ? 2 : 4;
			const uint32_t channels = data.m_ResourceFormat == RHIFormat::R32G32B32A32Float ? 4 : 2;
			const std::byte* source = data.m_Pixels.data() + subresource.m_DataOffset +
				y * subresource.m_RowPitch + (x * channels + channel) * channelBytes;
			if (channelBytes == 4)
			{
				float value;
				std::memcpy(&value, source, sizeof(value));
				return value;
			}
			uint16_t half;
			std::memcpy(&half, source, sizeof(half));
			const uint32_t exponent = (half >> 10) & 31;
			const uint32_t mantissa = half & 1023;
			const double magnitude = exponent == 0 ? std::ldexp(static_cast<double>(mantissa), -24)
				: exponent == 31 ? (mantissa == 0 ? std::numeric_limits<double>::infinity() : std::numeric_limits<double>::quiet_NaN())
				: std::ldexp(1.0 + mantissa / 1024.0, static_cast<int>(exponent) - 15);
			return (half & 0x8000) ? -magnitude : magnitude;
		}

		[[nodiscard]] BrdfIntegral SampleLut(const TextureAssetData& data, double noV, double roughness) noexcept
		{
			const uint32_t size = data.m_Extent.m_Width;
			const double x = std::clamp(noV * size - 0.5, 0.0, size - 1.0);
			const double y = std::clamp(roughness * size - 0.5, 0.0, size - 1.0);
			const auto x0 = static_cast<uint32_t>(x);
			const auto y0 = static_cast<uint32_t>(y);
			const uint32_t x1 = std::min(x0 + 1, size - 1);
			const uint32_t y1 = std::min(y0 + 1, size - 1);
			const auto sample = [&](uint32_t channel)
			{
				return std::lerp(std::lerp(ReadChannel(data, x0, y0, channel), ReadChannel(data, x1, y0, channel), x - x0),
					std::lerp(ReadChannel(data, x0, y1, channel), ReadChannel(data, x1, y1, channel), x - x0), y - y0);
			};
			return { sample(0), sample(1) };
		}

		void ValidateNumbers(SelfTestContext& context, const TextureAssetData& lut,
			const TextureAssetData& furnace) noexcept
		{
			const uint32_t size = lut.m_Extent.m_Width;
			bool finite = true;
			double maximumAlbedo = 0.0;
			for (uint32_t y = 0; y < size; ++y)
			{
				for (uint32_t x = 0; x < size; ++x)
				{
					const double a = ReadChannel(lut, x, y, 0);
					const double b = ReadChannel(lut, x, y, 1);
					finite &= std::isfinite(a) && std::isfinite(b) && a >= 0.0 && b >= 0.0;
					maximumAlbedo = std::max(maximumAlbedo, a + b);
				}
			}
			context.Check(finite, "Every baked LUT texel is finite and nonnegative");
			context.Check(maximumAlbedo <= 1.0 + FurnaceTolerance,
				std::format("Full LUT single-scattering albedo stays bounded: maximum {:.6f}", maximumAlbedo));
			// Include the actual first/last texel centers rather than rounding
			// edge probes to UV 0/1. Channels, axis order and row orientation matter.
			const std::array viewTexels{ 0u, size / 50, size / 20, size / 4, size / 2, size - 1 };
			const std::array roughnessTexels{ 0u, static_cast<uint32_t>(0.045 * size), size / 10,
				size / 4, size / 2, size * 3 / 4, size - 1 };
			double maximumError = 0.0;
			for (uint32_t y : roughnessTexels)
			{
				for (uint32_t x : viewTexels)
				{
					const double noV = (x + 0.5) / size;
					const double roughness = (y + 0.5) / size;
					const BrdfIntegral reference = ConvergedReference(context, noV, roughness);
					const BrdfIntegral actual{ ReadChannel(lut, x, y, 0), ReadChannel(lut, x, y, 1) };
					const double error = CoefficientError(actual, reference);
					maximumError = std::max(maximumError, error);
					const double tolerance = noV < 0.05 ? GrazingCoefficientTolerance : LutCoefficientTolerance;
					context.Check(error <= tolerance, std::format(
						"Baked A/B at ({}, {}): ({:.6f}, {:.6f}), reference ({:.6f}, {:.6f}), abs error {:.6f} <= {:.3f}",
						x, y, actual.m_A, actual.m_B, reference.m_A, reference.m_B, error, tolerance));
					// Independent unit-radiance direct integration multiplied by the
					// baked-LUT gain: unlike (A+B)/(A+B), this detects a biased LUT.
					const double white = (reference.m_A + reference.m_B) /
						std::clamp(actual.m_A + actual.m_B, 1.0e-4, 1.0);
					context.Check(std::isfinite(white) && std::abs(white - 1.0) <= FurnaceTolerance,
						std::format("Independent direct white furnace at ({}, {}): {:.6f}", x, y, white));
				}
			}

			double maximumSamplingError = 0.0;
			double maximumFurnaceError = 0.0;
			for (uint32_t y = 0; y < furnace.m_Extent.m_Height; ++y)
			{
				for (uint32_t x = 0; x < furnace.m_Extent.m_Width; ++x)
				{
					const double noV = (x + 0.5) / furnace.m_Extent.m_Width;
					const double roughness = (y + 0.5) / furnace.m_Extent.m_Height;
					const BrdfIntegral sampled = SampleLut(lut, noV, roughness);
					const BrdfIntegral reference = ConvergedReference(context, noV, roughness);
					const double albedo = std::clamp(sampled.m_A + sampled.m_B, 0.0, 1.0);
					const double referenceAlbedo = reference.m_A + reference.m_B;
					for (uint32_t channel = 0; channel < ProbeF0.size(); ++channel)
					{
						const double f0 = ProbeF0[channel];
						const double gain = albedo <= 1.0e-4 ? 1.0 : 1.0 + f0 * (1.0 / albedo - 1.0);
						const double expected = (f0 * sampled.m_A + sampled.m_B) * gain;
						const double actual = ReadChannel(furnace, x, y, channel);
						const double referenceGain = 1.0 + f0 * (1.0 / referenceAlbedo - 1.0);
						const double referenceFurnace = (f0 * reference.m_A + reference.m_B) * referenceGain;
						maximumFurnaceError = std::max(maximumFurnaceError, std::abs(actual - referenceFurnace));
						maximumSamplingError = std::max(maximumSamplingError, std::abs(actual - expected));
						context.Check(std::isfinite(actual) && actual >= 0.0 && actual <= 1.01 &&
							std::abs(actual - expected) < 0.003,
							std::format("GPU filtered LUT compensation at ({}, {}), F0={:.2f}", x, y, f0));
						context.Check(std::isfinite(referenceFurnace) && std::abs(actual - referenceFurnace) <= FurnaceTolerance,
							std::format("GPU white furnace vs independent integral at ({}, {}), F0={:.2f}: {:.6f} vs {:.6f}",
								x, y, f0, actual, referenceFurnace));
					}
				}
			}
			std::printf("BRDF LUT %u: maximum A/B absolute error %.6f; GPU filtering/compensation error %.6f; furnace reference error %.6f\n",
				size, maximumError, maximumSamplingError, maximumFurnaceError);
		}

		void BakeAndValidate(SelfTestContext& context, RHIContext& rhi,
			RHIBindingLayoutHandle lutLayout, RHIBindingLayoutHandle furnaceLayout,
			const ShaderCompileResult& vertex, const ShaderCompileResult& pixel,
			const ShaderCompileResult& furnacePixel, const IBLBakeConfig& config) noexcept
		{
			auto& device = rhi.GetDevice();
			auto& pipelines = rhi.GetPipelineSystem();
			const uint32_t size = config.m_BrdfLutSize;
			const RHITextureDesc lutDesc{ .m_Format = config.m_BrdfLutFormat,
				.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled | RHITextureUsage::CopySource,
				.m_Extent = { size, size, 1 }, .m_DebugName = "Numeric.BrdfLut" };
			const RHITextureDesc furnaceDesc{ .m_Format = RHIFormat::R32G32B32A32Float,
				.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::CopySource,
				.m_Extent = { 11, 9, 1 }, .m_DebugName = "Numeric.WhiteFurnace" };
			RHITextureOwner lut(&device, device.CreateTexture({ .m_Desc = lutDesc }));
			RHITextureOwner furnace(&device, device.CreateTexture({ .m_Desc = furnaceDesc }));
			context.Check(lut && furnace, "LUT and furnace render targets are created");
			if (!lut || !furnace)
			{
				return;
			}
			const RHITextureViewDesc rtvDesc{ .m_Type = RHITextureViewType::RenderTarget,
				.m_Dimension = RHITextureViewDimension::Texture2D };
			const auto lutRtv = device.CreateTextureView(lut.Get(), rtvDesc);
			const auto furnaceRtv = device.CreateTextureView(furnace.Get(), rtvDesc);
			const auto lutSrv = device.CreateTextureView(lut.Get(), {
				.m_Type = RHITextureViewType::ShaderResource, .m_Dimension = RHITextureViewDimension::Texture2D });
			const auto sampler = device.CreateSampler({});
			const auto lutPipeline = CreatePipeline(pipelines, lutLayout, vertex, pixel, lutDesc.m_Format, "PSMain");
			const auto furnacePipeline = CreatePipeline(pipelines, furnaceLayout, vertex, furnacePixel, furnaceDesc.m_Format, "PSWhiteFurnace");
			const bool ready = lutRtv.IsValid() && furnaceRtv.IsValid() && lutSrv.IsValid() && sampler.IsValid() &&
				lutPipeline.IsValid() && furnacePipeline.IsValid() &&
				device.PublishTextureViewDescriptor(lutSrv) && device.PublishSamplerDescriptor(sampler);
			context.Check(ready, "Production LUT and furnace pipelines/descriptors are ready on the owner thread");
			if (ready)
			{
				const auto begin = rhi.BeginFrame();
				context.Check(begin.IsReady(), "Hidden-window GPU frame begins");
				if (begin.IsReady())
				{
					auto& commands = begin.GetFrame()->GetGraphicsContext();
					Transition(commands, lut.Get(), UndefinedRHITextureState(), RenderTargetState);
					Draw(commands, lutRtv, lutPipeline, size, size);
					Transition(commands, lut.Get(), RenderTargetState, SampleState);
					Transition(commands, furnace.Get(), UndefinedRHITextureState(), RenderTargetState);
					const ProbeParameters parameters{ device.GetTextureViewDescriptor(lutSrv).m_Index,
						device.GetSamplerDescriptor(sampler).m_Index };
					commands.TrackTextureUse(lut.Get());
					Draw(commands, furnaceRtv, furnacePipeline,
						furnaceDesc.m_Extent.m_Width, furnaceDesc.m_Extent.m_Height, &parameters);
					// TransferBatch::ReadbackTexture borrows textures in Common and
					// restores Common after its own CopySource transition.
					Transition(commands, lut.Get(), SampleState, CommonRHIResourceState());
					Transition(commands, furnace.Get(), RenderTargetState, CommonRHIResourceState());
					// Discard the hidden backbuffer's prior contents on every acquire.
					const auto backbuffer = begin.GetFrame()->GetBackBuffer();
					const auto backbufferRtv = device.CreateTextureView(backbuffer, rtvDesc);
					Transition(commands, backbuffer, UndefinedRHITextureState(), RenderTargetState);
					const RHIRenderingAttachment hiddenAttachment{ .m_View = backbufferRtv,
						.m_LoadOp = RHIContentLoadOp::DontCare };
					commands.BeginRendering({ .m_ColorAttachments = std::span(&hiddenAttachment, 1) });
					commands.ClearColorAttachment(0, { 0.0f, 0.0f, 0.0f, 1.0f });
					commands.EndRendering();
					Transition(commands, backbuffer, RenderTargetState, PresentRHITextureState());
					const auto end = rhi.EndFrame(*begin.GetFrame());
					rhi.WaitIdle();
					const bool completed = end.IsCompleted() && end.HasSubmission() &&
						device.IsFencePointCompleted(end.GetSubmittedFence());
					context.Check(completed, "LUT draw completes before transfer readback or destruction");
					device.DestroyTextureView(backbufferRtv);
					if (completed)
					{
						const auto lutData = Readback(context, rhi, lut.Get(), lutDesc);
						const auto furnaceData = Readback(context, rhi, furnace.Get(), furnaceDesc);
						if (lutData.IsValid() && furnaceData.IsValid())
						{
							ValidateNumbers(context, lutData, furnaceData);
						}
					}
				}
			}
			device.DestroySampler(sampler);
			device.DestroyTextureView(lutSrv);
			device.DestroyTextureView(furnaceRtv);
			device.DestroyTextureView(lutRtv);
		}

		void RunGpuTests(SelfTestContext& context, RHIBackendType backend) noexcept
		{
			const auto runtimeRoot = win32::GetExecutableDirectory();
			ShaderCompiler compiler(ResolveShaderSourceRoot(runtimeRoot), ResolveShaderCacheRoot(runtimeRoot));
			const auto compile = [&](const wchar_t* source, ShaderStage stage, const wchar_t* entry)
			{
				ShaderDesc desc{ .m_SourcePath = source, .m_Stage = stage, .m_Entry = entry, .m_IncludeDirs = { L"." } };
				desc.m_Target = backend == RHIBackendType::DX12 ? MakeDX12CompileTarget(stage) : MakeVulkan13CompileTarget(stage);
				return compiler.Compile(desc);
			};
			const auto vertex = compile(L"Passes/PassIBLBrdfLUT.hlsl", ShaderStage::Vertex, L"VSMain");
			const auto pixel = compile(L"Passes/PassIBLBrdfLUT.hlsl", ShaderStage::Pixel, L"PSMain");
			const auto furnacePixel = compile(L"Tests/BrdfLutWhiteFurnace.hlsl", ShaderStage::Pixel, L"PSWhiteFurnace");
			context.Check(vertex.IsSuccess() && pixel.IsSuccess() && furnacePixel.IsSuccess(),
				"Production BRDF LUT and numeric probe shaders compile for the selected backend");
			if (!vertex.IsSuccess() || !pixel.IsSuccess() || !furnacePixel.IsSuccess())
			{
				return;
			}
			HiddenTestWindow window;
			context.Check(window.Get() != nullptr, "Test host creates a hidden window");
			if (!window.Get())
			{
				return;
			}
			const RHIContextDesc desc{ .m_Width = 64, .m_Height = 64, .m_EnableDebugValidation = true };
			std::unique_ptr<RHIContext> rhi;
			if (backend == RHIBackendType::DX12)
			{
				rhi = CreateDX12Context(desc, window.Get());
			}
#if GGLAB_ENABLE_VULKAN
			else
			{
				rhi = CreateVulkanWin32Context(desc, GetModuleHandleW(nullptr), window.Get(), sizeof(void*) == 8);
			}
#endif
			context.Check(rhi != nullptr, "Selected RHI initializes without backend fallback");
			if (!rhi)
			{
				return;
			}
			std::printf("BRDF LUT adapter identity: %s\n", std::string(rhi->GetDevice().GetAdapterCompatibilityIdentity()).c_str());
			RHIBindingLayoutDesc layoutDesc{};
			layoutDesc.m_Slots[0] = { .m_Type = RHIBindingType::PushConstants, .m_Visibility = RHIShaderStage::Pixel,
				.m_Binding = 2, .m_SizeInBytes = 16 };
			layoutDesc.m_Slots[1] = { .m_Type = RHIBindingType::BindlessResourceTable, .m_Visibility = RHIShaderStage::Pixel, .m_Count = 0 };
			layoutDesc.m_Slots[2] = { .m_Type = RHIBindingType::BindlessSamplerTable, .m_Visibility = RHIShaderStage::Pixel, .m_Count = 0 };
			layoutDesc.m_SlotCount = 3;
			const auto furnaceLayout = rhi->GetPipelineSystem().CreateBindingLayout(layoutDesc);
			const auto lutLayout = rhi->GetPipelineSystem().CreateBindingLayout({});
			context.Check(lutLayout.IsValid() && furnaceLayout.IsValid(), "LUT and numeric probe binding layouts are created");
			if (lutLayout.IsValid() && furnaceLayout.IsValid())
			{
				for (const auto preset : { IBLQualityPreset::Low, IBLQualityPreset::Medium, IBLQualityPreset::Offline })
				{
					std::printf("Validating %s BRDF LUT preset\n", std::string(GetIBLQualityPresetName(preset)).c_str());
					BakeAndValidate(context, *rhi, lutLayout, furnaceLayout, vertex, pixel, furnacePixel, GetIBLBakeConfig(preset));
				}
			}
			rhi->WaitIdle();
			rhi->RetireCompletedWork();
#if GGLAB_ENABLE_VULKAN
			if (backend == RHIBackendType::Vulkan)
			{
				VulkanBackendSnapshot snapshot{};
				BuildVulkanBackendSnapshot(static_cast<const VulkanContext&>(*rhi), snapshot);
				context.Check(snapshot.m_ValidationEnabled && snapshot.m_ValidationErrors == 0 && snapshot.m_ValidationWarnings == 0,
					"Vulkan validation is active with no errors or warnings");
			}
#endif
#if defined(_DEBUG)
			if (backend == RHIBackendType::DX12)
			{
				ComPtr<ID3D12InfoQueue> queue;
				auto& device = static_cast<DX12Device&>(rhi->GetDevice());
				const bool active = SUCCEEDED(device.Get()->QueryInterface(IID_PPV_ARGS(&queue)));
				bool clean = active;
				if (active)
				{
					for (uint64_t index = 0; index < queue->GetNumStoredMessages(); ++index)
					{
						SIZE_T bytes = 0;
						queue->GetMessage(index, nullptr, &bytes);
						std::vector<std::byte> storage(bytes);
						auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
						if (FAILED(queue->GetMessage(index, message, &bytes)))
						{
							clean = false;
							continue;
						}
						if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
						{
							clean = false;
							std::printf("D3D12 validation: %s\n", message->pDescription);
						}
					}
				}
				context.Check(clean, "D3D12 debug layer is active with no errors or warnings");
			}
#endif
		}
	}

	void RunBrdfLutReferenceSelfTests(SelfTestContext& context) noexcept
	{
		const auto rough = ConvergedReference(context, 1.0, 1.0);
		context.Check(std::abs(rough.m_A + rough.m_B - (1.0 - std::log(2.0))) < 0.00001,
			"Independent GGX quadrature matches analytic normal-incidence alpha=1 albedo 1-ln(2)");
		const auto mirror = ConvergedReference(context, 0.5, 0.0001);
		const double schlick = std::pow(1.0 - 0.5, 5.0);
		context.Check(std::abs(mirror.m_A - (1.0 - schlick)) < 0.00001 && std::abs(mirror.m_B - schlick) < 0.00001,
			"Smooth-limit A/B matches the analytic Schlick Fresnel split");
		const auto grazing = ConvergedReference(context, 0.02, 0.5);
		context.Check(std::isfinite(grazing.m_A) && std::isfinite(grazing.m_B) && grazing.m_A > 0.0 &&
			grazing.m_B > 0.0 && grazing.m_A + grazing.m_B <= 1.0,
			"Independent grazing integration is finite, positive and energy bounded");
	}

	void RunDX12BrdfLutNumericSelfTests(SelfTestContext& context) noexcept
	{
		RunGpuTests(context, RHIBackendType::DX12);
	}

	void RunVulkanBrdfLutNumericSelfTests(SelfTestContext& context) noexcept
	{
		RunGpuTests(context, RHIBackendType::Vulkan);
	}
}
