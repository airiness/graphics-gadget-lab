#include "FrameCaptureSelfTests.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchDefaults.h"

#include "GGLabRuntime/Core/Time.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/Capture/ResourceExportTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlusDebugReadback.h"
#include "Graphics/Capture/FrameCaptureService.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Diagnostics/RenderGraphEnumText.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

// Headless contracts for frame capture: copy footprints, display-byte
// conversion and the capture service's request lifecycle. The fake device
// stores readback bytes and completes fences on demand; it records no GPU work,
// so these tests do not establish GPU copy correctness.
namespace gglab
{
	namespace
	{
		class CaptureTestDevice final : public RHIDevice
		{
		public:
			RHIBackendType GetBackendType() const noexcept override { return RHIBackendType::DX12; }
			std::string_view GetAdapterCompatibilityIdentity() const noexcept override
			{
				return "GGLab.FrameCaptureTestDevice";
			}
			RHIShaderWaveCapabilities GetShaderWaveCapabilities() const noexcept override
			{
				return { true, 32, 32 };
			}
			RHITextureSupportResult QueryTextureSupport(const RHITextureDesc&) const noexcept override
			{
				return { .m_Supported = true };
			}
			RHITextureSupportResult QueryTextureViewSupport(
				const RHITextureDesc&, const RHITextureViewDesc&) const noexcept override
			{
				return { .m_Supported = true };
			}
			RHITextureHandle CreateTexture(const RHIOwnedTextureCreateInfo&,
				const RHIResourceDebugIdentityDesc&) noexcept override
			{
				return RHITextureHandle{ m_NextTextureIndex++, 1 };
			}
			RHIBufferHandle CreateBuffer(
				const RHIBufferDesc& desc, const RHIResourceDebugIdentityDesc&) noexcept override
			{
				if (m_FailBufferCreation)
				{
					return {};
				}
				const RHIBufferHandle handle{ m_NextBufferIndex++, 1 };
				m_Buffers[handle] = Buffer{ .m_Desc = desc,
					.m_Bytes = std::vector<uint8_t>(desc.m_SizeInBytes) };
				return handle;
			}
			RHITextureViewHandle CreateTextureView(
				RHITextureHandle, const RHITextureViewDesc&) noexcept override
			{
				return {};
			}
			RHIBufferViewHandle CreateBufferView(
				RHIBufferHandle, const RHIBufferViewDesc&) noexcept override
			{
				return {};
			}
			RHISamplerHandle CreateSampler(const RHISamplerDesc&) noexcept override { return {}; }
			void DestroyTexture(RHITextureHandle) noexcept override {}
			void DestroyBuffer(RHIBufferHandle buffer) noexcept override { m_Buffers.erase(buffer); }
			void DestroyTextureView(RHITextureViewHandle) noexcept override {}
			void DestroyBufferView(RHIBufferViewHandle) noexcept override {}
			void DestroySampler(RHISamplerHandle) noexcept override {}
			void SetTextureDebugBinding(
				RHITextureHandle, const RHIResourceDebugBindingDesc&) noexcept override
			{
			}
			void SetBufferDebugBinding(
				RHIBufferHandle, const RHIResourceDebugBindingDesc&) noexcept override
			{
			}
			std::string_view GetTextureDebugName(RHITextureHandle) const noexcept override
			{
				return {};
			}
			std::string_view GetBufferDebugName(RHIBufferHandle) const noexcept override
			{
				return {};
			}
			void* MapBuffer(RHIBufferHandle buffer, RHIMappedBufferRange) noexcept override
			{
				const auto entry = m_Buffers.find(buffer);
				if (entry == m_Buffers.end() || m_FailMapping)
				{
					return nullptr;
				}
				++m_MapCount;
				if (entry->second.m_Desc.m_MemoryUsage == RHIMemoryUsage::GpuToCpu) { ++m_ReadbackMapCalls; }
				return entry->second.m_Bytes.data();
			}
			void UnmapBuffer(RHIBufferHandle, RHIMappedBufferRange) noexcept override
			{
				--m_MapCount;
			}
			uint32_t GetBufferViewAlignment(RHIBufferViewType) const noexcept override { return 1; }
			bool IsAlive(RHITextureHandle texture) const noexcept override { return texture.IsValid(); }
			bool IsAlive(RHIBufferHandle buffer) const noexcept override
			{
				return m_Buffers.contains(buffer);
			}
			bool IsAlive(RHISamplerHandle) const noexcept override { return false; }
			bool IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept override
			{
				return fencePoint.IsValid() && fencePoint.m_Value <= m_CompletedFenceValue;
			}
			void RecordTextureUse(RHITextureHandle, const RHIFencePoint&) noexcept override {}
			void RecordBufferUse(RHIBufferHandle, const RHIFencePoint&) noexcept override {}
			RHIDescriptorHandle GetTextureViewDescriptor(RHITextureViewHandle) const noexcept override
			{
				return {};
			}
			RHIDescriptorHandle GetBufferViewDescriptor(RHIBufferViewHandle) const noexcept override
			{
				return {};
			}
			RHIDescriptorHandle GetSamplerDescriptor(RHISamplerHandle) const noexcept override
			{
				return {};
			}
			void RetireCompletedWork() noexcept override {}

			// Simulates the GPU copy: row y, byte x holds (y * 16 + x) in the row
			// payload and 0xEE in the pitch padding.
			void FillReadback(RHIBufferHandle buffer, const RHITextureCopyFootprint& footprint) noexcept
			{
				std::vector<uint8_t>& bytes = m_Buffers.at(buffer).m_Bytes;
				for (uint64_t row = 0; row < footprint.m_Height; ++row)
				{
					for (uint64_t column = 0; column < footprint.m_RowPitch; ++column)
					{
						bytes[row * footprint.m_RowPitch + column] =
							column < footprint.m_RowSizeInBytes
							? static_cast<uint8_t>(row * 16 + column)
							: uint8_t{ 0xEE };
					}
				}
			}

			void CopyBufferBytes(RHIBufferHandle destination, uint64_t destinationOffset,
				RHIBufferHandle source, uint64_t sourceOffset, uint64_t bytes) noexcept
			{
				std::memcpy(m_Buffers.at(destination).m_Bytes.data() + destinationOffset,
					m_Buffers.at(source).m_Bytes.data() + sourceOffset, static_cast<size_t>(bytes));
			}

			[[nodiscard]] const RHIBufferDesc* FindBufferDesc(RHIBufferHandle buffer) const noexcept
			{
				const auto entry = m_Buffers.find(buffer);
				return entry != m_Buffers.end() ? &entry->second.m_Desc : nullptr;
			}
			[[nodiscard]] size_t GetLiveBufferCount() const noexcept { return m_Buffers.size(); }
			[[nodiscard]] int32_t GetMapCount() const noexcept { return m_MapCount; }

			uint32_t m_ReadbackMapCalls = 0;
			uint64_t m_CompletedFenceValue = 0;
			bool m_FailBufferCreation = false;
			bool m_FailMapping = false;

		private:
			struct Buffer
			{
				RHIBufferDesc m_Desc{};
				std::vector<uint8_t> m_Bytes;
			};

			std::unordered_map<RHIBufferHandle, Buffer> m_Buffers;
			uint32_t m_NextBufferIndex = 0;
			uint32_t m_NextTextureIndex = 0;
			int32_t m_MapCount = 0;
		};

		class ExportTestCommands final : public RHIGraphicsCommandContext
		{
		public:
			explicit ExportTestCommands(CaptureTestDevice& device) : m_Device(device) {}
			RHICommandContextHandle GetHandle() const noexcept override { return {}; }
			RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Graphics; }
			void TrackTextureUse(RHITextureHandle) noexcept override {}
			void TrackBufferUse(RHIBufferHandle) noexcept override {}
			void TextureBarrier(std::span<const RHITextureBarrier>) noexcept override {}
			void BufferBarrier(std::span<const RHIBufferBarrier>) noexcept override {}
			void FlushBarriers() noexcept override {}
			void CopyBuffer(RHIBufferHandle destination, uint64_t destinationOffset,
				RHIBufferHandle source, uint64_t sourceOffset, uint64_t bytes) noexcept override
			{
				m_Device.CopyBufferBytes(destination, destinationOffset, source, sourceOffset, bytes);
			}
			void CopyTextureToBuffer(const RHITextureToBufferCopy& copy) noexcept override
			{
				m_TextureCopies.push_back(copy);
				auto* bytes = static_cast<uint8_t*>(m_Device.MapBuffer(copy.m_Destination, {})) + copy.m_DestinationOffset;
				const auto& fp = copy.m_Footprint;
				for (uint32_t row = 0; row < fp.m_Height; ++row)
				{
					std::memset(bytes + row * fp.m_RowPitch, 0xee, static_cast<size_t>(fp.m_RowPitch));
					for (uint32_t x = 0; x < fp.m_Width; ++x)
					{
						if (copy.m_SourceAspect == RHITextureAspect::Depth)
						{
							const uint32_t depth = x == 0 ? 0u : 0x00ffffffu;
							std::memcpy(bytes + row * fp.m_RowPitch + x * 4, &depth, 4);
						}
						else
						{
							std::memset(bytes + row * fp.m_RowPitch + x * fp.m_BytesPerTexel,
								static_cast<int>(copy.m_SourceArraySlice * 32 + row * 8 + x), fp.m_BytesPerTexel);
						}
					}
				}
				m_Device.UnmapBuffer(copy.m_Destination, {});
			}
			void SetPipeline(RHIPipelineHandle) noexcept override {}
			void SetDescriptorTable(const RHIDescriptorTableBinding&) noexcept override {}
			void BeginRendering(const RHIRenderingInfo&) noexcept override {}
			void EndRendering() noexcept override {}
			bool IsRendering() const noexcept override { return false; }
			void ClearColorAttachment(uint32_t, const std::array<float, 4>&) noexcept override {}
			void ClearDepthAttachment(float, std::optional<uint8_t>) noexcept override {}
			void SetViewport(const RHIViewport&) noexcept override {}
			void SetScissorRect(const RHIScissorRect&) noexcept override {}
			void SetPrimitiveTopology(RHIPrimitiveTopology) noexcept override {}
			void SetConstantBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
			void SetReadOnlyBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
			void SetPushConstants(uint32_t, std::span<const uint32_t>, uint32_t) noexcept override {}
			void SetVertexBuffers(uint32_t, std::span<const RHIVertexBufferBinding>) noexcept override {}
			void SetIndexBuffer(const RHIIndexBufferBinding&) noexcept override {}
			void DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) noexcept override {}
			void Draw(uint32_t, uint32_t, uint32_t, uint32_t) noexcept override {}
			std::vector<RHITextureToBufferCopy> m_TextureCopies;
		private:
			CaptureTestDevice& m_Device;
		};

		// Test-only pipeline identities and CPU dispatch simulation exercise the
		// service lifecycle, not shader or GPU correctness.
		class ScanTestPrograms final : public RenderShaderProgramAccess, public RenderPipelineResolver, public RHIPipelineSystem
		{
		public:
			ShaderID LoadProgram(const ShaderProgramRef& ref) noexcept override
			{
				++m_LoadCount;
				return ShaderID{ ref == shader_programs::NonFiniteScanClearCompute ? 0u :
					ref == shader_programs::NonFiniteScanTextureCompute ? 1u : 2u };
			}
			uint64_t GetGeneration(ShaderID) const noexcept override { return 1; }
			RHIPipelineHandle Resolve(GraphicsPipelineSlot&, const GraphicsPhysicalPipelineKey&, const RenderPassInfo&) noexcept override { return {}; }
			RHIPipelineHandle Resolve(ComputePipelineSlot&, const ComputePipelineRecipe& recipe, const RenderPassInfo&) noexcept override
			{
				return m_FailResolution ? RHIPipelineHandle{} : RHIPipelineHandle{ recipe.m_CSId.Value(), 1 };
			}
			void GetPipelineUsages(RHIPipelineHandle, std::vector<RenderPassInfo>&) const noexcept override {}
			RHIBindingLayoutHandle CreateBindingLayout(const RHIBindingLayoutDesc&) noexcept override { ++m_LayoutCount; return { 0, 1 }; }
			RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineCreateInfo&) noexcept override { return {}; }
			RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineCreateInfo&) noexcept override { return {}; }
			bool IsAlive(RHIBindingLayoutHandle handle) const noexcept override { return handle.IsValid(); }
			bool IsAlive(RHIPipelineHandle handle) const noexcept override { return handle.IsValid(); }
			uint64_t GetRevision() const noexcept override { return 1; }
			void Clear() noexcept override {}
			uint32_t m_LoadCount = 0;
			uint32_t m_LayoutCount = 0;
			bool m_FailResolution = false;
		};

		class ScanTestCommands final : public RHIComputeCommandContext
		{
		public:
			explicit ScanTestCommands(CaptureTestDevice& device) : m_Device(device) {}
			RHICommandContextHandle GetHandle() const noexcept override { return {}; }
			RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Graphics; }
			void TrackTextureUse(RHITextureHandle) noexcept override {}
			void TrackBufferUse(RHIBufferHandle) noexcept override {}
			void TextureBarrier(std::span<const RHITextureBarrier>) noexcept override {}
			void BufferBarrier(std::span<const RHIBufferBarrier>) noexcept override {}
			void FlushBarriers() noexcept override {}
			void CopyBuffer(RHIBufferHandle, uint64_t, RHIBufferHandle, uint64_t, uint64_t) noexcept override {}
			void CopyTextureToBuffer(const RHITextureToBufferCopy&) noexcept override {}
			void SetPipeline(RHIPipelineHandle pipeline) noexcept override { m_Pipeline = pipeline; }
			void SetDescriptorTable(const RHIDescriptorTableBinding&) noexcept override {}
			void SetConstantBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
			void SetReadOnlyBuffer(uint32_t slot, RHIBufferHandle buffer, uint64_t) noexcept override
			{
				if (slot == 1) { m_Source = buffer; } else { m_Components = buffer; }
			}
			void SetReadWriteBuffer(uint32_t, RHIBufferHandle buffer, uint64_t) noexcept override { m_Record = buffer; }
			void SetPushConstants(uint32_t, std::span<const uint32_t> words, uint32_t) noexcept override
			{
				std::memcpy(&m_Parameters, words.data(), sizeof(m_Parameters));
			}
			void Dispatch(uint32_t, uint32_t, uint32_t) noexcept override
			{
				++m_Dispatches;
				auto* record = static_cast<NonFiniteScanRecord*>(m_Device.MapBuffer(m_Record, {}));
				if (m_Pipeline.Index() == 0) { *record = {}; }
				else if (!m_SkipScan)
				{
					const auto* source = static_cast<const uint8_t*>(m_Device.MapBuffer(m_Source, {}));
					const auto* components = static_cast<const NonFiniteBufferComponent*>(m_Device.MapBuffer(m_Components, {}));
					for (uint32_t i = 0; i < m_Parameters.m_ScalarCount; ++i)
					{
						uint32_t bits;
						std::memcpy(&bits, source + (i / m_Parameters.m_ComponentCount) * m_Parameters.m_BufferStride +
							components[i % m_Parameters.m_ComponentCount].m_Offset, 4);
						AccumulateNonFiniteScanValue(*record, bits, i);
					}
					m_Device.UnmapBuffer(m_Components, {}); m_Device.UnmapBuffer(m_Source, {});
				}
				m_Device.UnmapBuffer(m_Record, {});
			}
			uint32_t m_Dispatches = 0;
			bool m_SkipScan = false;
		private:
			CaptureTestDevice& m_Device;
			RHIPipelineHandle m_Pipeline;
			RHIBufferHandle m_Source, m_Components, m_Record;
			NonFiniteScanParameters m_Parameters;
		};

		[[nodiscard]] RHIFencePoint MakeFence(uint64_t value) noexcept
		{
			return { RHIFenceHandle{ 1, 1 }, value };
		}

		[[nodiscard]] RHITextureDesc MakeDisplayTarget(RHIFormat format, uint32_t width,
			uint32_t height, bool copySource = true) noexcept
		{
			return RHITextureDesc{
				.m_Format = format,
				.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Present |
					(copySource ? RHITextureUsage::CopySource : RHITextureUsage::None),
				.m_Extent = { width, height, 1u },
			};
		}

		[[nodiscard]] std::vector<FrameCaptureResult> Consume(FrameCaptureService& service) noexcept
		{
			std::vector<FrameCaptureResult> results;
			service.ConsumeResults(results);
			return results;
		}

		void RunFootprintTests(SelfTestContext& context) noexcept
		{
			const RHITextureCopyFootprint narrow =
				ComputeRHITextureCopyFootprint(RHIFormat::R8G8B8A8Unorm, 3, 2);
			context.Check(narrow.IsValid() && narrow.m_BytesPerTexel == 4 &&
				narrow.m_RowSizeInBytes == 12 && narrow.m_RowPitch == 256 &&
				narrow.m_SizeInBytes == 512,
				"Copy footprints align each row to the neutral row pitch");

			const RHITextureCopyFootprint exact =
				ComputeRHITextureCopyFootprint(RHIFormat::B8G8R8A8UnormSrgb, 64, 1);
			const RHITextureCopyFootprint padded =
				ComputeRHITextureCopyFootprint(RHIFormat::B8G8R8A8UnormSrgb, 65, 1);
			context.Check(exact.m_RowPitch == 256 && padded.m_RowPitch == 512 &&
				padded.m_RowSizeInBytes == 260,
				"Rows that fill the alignment exactly are not padded further");

			const RHITextureCopyFootprint wide =
				ComputeRHITextureCopyFootprint(RHIFormat::R16G16B16A16Float, 40, 3);
			context.Check(wide.IsValid() && wide.m_BytesPerTexel == 8 &&
				wide.m_RowPitch == 512 && wide.m_SizeInBytes == 1536,
				"64-bit color formats produce a placed footprint");

			context.Check(!ComputeRHITextureCopyFootprint(RHIFormat::R8G8B8A8Unorm, 0, 4).IsValid() &&
				!ComputeRHITextureCopyFootprint(RHIFormat::D24UnormS8Uint, 8, 8).IsValid() &&
				!ComputeRHITextureCopyFootprint(RHIFormat::D32Float, 8, 8).IsValid() &&
				!ComputeRHITextureCopyFootprint(RHIFormat::R8G8B8A8Typeless, 8, 8).IsValid() &&
				!ComputeRHITextureCopyFootprint(RHIFormat::R32G32B32Float, 8, 8).IsValid(),
				"Empty, depth-stencil, depth, typeless and 96-bit targets have no footprint");
		}

		void RunConversionTests(SelfTestContext& context) noexcept
		{
			const FrameCaptureImage bgra{
				.m_Format = RHIFormat::B8G8R8A8UnormSrgb,
				.m_Width = 2,
				.m_Height = 1,
				.m_Pixels = { 10, 20, 30, 0, 40, 50, 60, 7 },
			};
			const std::optional<std::vector<uint8_t>> fromBgra = ConvertFrameCaptureToRgba8(bgra);
			context.Check(fromBgra &&
				*fromBgra == std::vector<uint8_t>{ 30, 20, 10, 255, 60, 50, 40, 255 },
				"BGRA display bytes convert to opaque RGBA without transfer conversion");

			FrameCaptureImage rgba = bgra;
			rgba.m_Format = RHIFormat::R8G8B8A8Unorm;
			const std::optional<std::vector<uint8_t>> fromRgba = ConvertFrameCaptureToRgba8(rgba);
			context.Check(fromRgba &&
				*fromRgba == std::vector<uint8_t>{ 10, 20, 30, 255, 40, 50, 60, 255 },
				"RGBA display bytes keep their channel order with opaque alpha");

			FrameCaptureImage unsupported = bgra;
			unsupported.m_Format = RHIFormat::R16G16B16A16Float;
			FrameCaptureImage truncated = bgra;
			truncated.m_Pixels.pop_back();
			context.Check(!ConvertFrameCaptureToRgba8(unsupported) &&
				!ConvertFrameCaptureToRgba8(truncated),
				"Formats without an 8-bit display conversion and malformed images are rejected");
		}

		void RunCompletionTests(SelfTestContext& context) noexcept
		{
			CaptureTestDevice device;
			{
				FrameCaptureService service(device);
				const uint64_t sceneA = service.RequestCapture(FrameCaptureSource::Scene);
				const uint64_t composited = service.RequestCapture(FrameCaptureSource::Composited);
				const uint64_t sceneB = service.RequestCapture(FrameCaptureSource::Scene);
				context.Check(sceneA != 0 && composited > sceneA && sceneB > composited &&
					service.GetUnfinishedRequestCount() == 3 &&
					service.HasPendingRequests(FrameCaptureSource::Scene) &&
					service.HasPendingRequests(FrameCaptureSource::Composited),
					"Requests receive increasing non-zero ids and stay queued per source");

				const RHITextureDesc target = MakeDisplayTarget(RHIFormat::B8G8R8A8Unorm, 3, 2);
				const std::optional<FrameCaptureTapTarget> tap =
					service.BindTap(7, FrameCaptureSource::Scene, target);
				const RHIBufferDesc* bufferDesc = tap ? device.FindBufferDesc(tap->m_Buffer) : nullptr;
				context.Check(tap && bufferDesc && tap->m_Footprint.m_RowPitch == 256 &&
					bufferDesc->m_SizeInBytes == tap->m_Footprint.m_SizeInBytes &&
					bufferDesc->m_MemoryUsage == RHIMemoryUsage::GpuToCpu &&
					Test(bufferDesc->m_Usage, RHIBufferUsage::CopyDest) &&
					!service.HasPendingRequests(FrameCaptureSource::Scene) &&
					service.HasPendingRequests(FrameCaptureSource::Composited) &&
					!service.BindTap(7, FrameCaptureSource::Scene, target),
					"Binding a tap claims every queued request of its source only");
				if (!tap)
				{
					return;
				}

				device.FillReadback(tap->m_Buffer, tap->m_Footprint);
				service.OnFrameSubmitted(7, MakeFence(3));
				service.CollectCompleted();
				context.Check(Consume(service).empty() && service.GetUnfinishedRequestCount() == 3 &&
					device.IsAlive(tap->m_Buffer),
					"A submitted tap keeps its readback buffer until the frame fence completes");

				device.m_CompletedFenceValue = 3;
				service.CollectCompleted();
				const std::vector<FrameCaptureResult> results = Consume(service);
				const bool completed = results.size() == 2 &&
					results[0].m_RequestId == sceneA && results[1].m_RequestId == sceneB &&
					results[0].m_Status == FrameCaptureStatus::Completed &&
					results[1].m_Status == FrameCaptureStatus::Completed &&
					results[0].m_FrameSerial == 7 && results[0].m_Image &&
					results[0].m_Image == results[1].m_Image;
				context.Check(completed && !device.IsAlive(tap->m_Buffer) &&
					device.GetMapCount() == 0 && service.GetUnfinishedRequestCount() == 1,
					"Completed taps publish one shared image per frame and release the readback");
				if (completed)
				{
					const FrameCaptureImage& image = *results[0].m_Image;
					const std::array<uint8_t, 4> firstRowStart{ 0, 1, 2, 3 };
					const std::array<uint8_t, 4> secondRowStart{ 16, 17, 18, 19 };
					context.Check(image.m_Format == RHIFormat::B8G8R8A8Unorm &&
						image.m_Width == 3 && image.m_Height == 2 && image.m_Pixels.size() == 24 &&
						std::equal(firstRowStart.begin(), firstRowStart.end(), image.m_Pixels.begin()) &&
						std::equal(secondRowStart.begin(), secondRowStart.end(),
							image.m_Pixels.begin() + 12) &&
						image.m_Pixels[11] == 11,
						"Captured rows are copied top-down without pitch padding");
				}

				service.Shutdown();
				const std::vector<FrameCaptureResult> shutdownResults = Consume(service);
				context.Check(shutdownResults.size() == 1 &&
					shutdownResults[0].m_RequestId == composited &&
					shutdownResults[0].m_Status == FrameCaptureStatus::Failed &&
					shutdownResults[0].m_FrameSerial == 0 && !shutdownResults[0].m_Failure.empty() &&
					service.GetUnfinishedRequestCount() == 0,
					"Shutdown fails requests that no frame recorded");

				const uint64_t late = service.RequestCapture(FrameCaptureSource::Scene);
				const std::vector<FrameCaptureResult> lateResults = Consume(service);
				context.Check(lateResults.size() == 1 && lateResults[0].m_RequestId == late &&
					lateResults[0].m_Status == FrameCaptureStatus::Failed &&
					service.GetUnfinishedRequestCount() == 0,
					"Requests after shutdown finish immediately as Failed");
			}
			context.Check(device.GetLiveBufferCount() == 0,
				"The capture service leaves no readback buffers alive");
		}

		void RunDiagnosticTests(SelfTestContext& context) noexcept
		{
			context.Check(GetFrameCaptureSourceName(FrameCaptureSource::Diagnostic) == "diagnostic" &&
				FindFrameCaptureDiagnosticTap("temporal-history-weight") ==
				PostProcessDebugTap::TemporalHistoryWeight &&
				GetFrameCaptureDiagnosticTapName(PostProcessDebugTap::TemporalRejection) ==
				"temporal-rejection" &&
				FindFrameCaptureDiagnosticTap("temporal-clip-distance") ==
				PostProcessDebugTap::TemporalClipDistance &&
				FindFrameCaptureDiagnosticTap("temporal-history-relaxation") ==
				PostProcessDebugTap::TemporalHistoryRelaxation &&
				FindFrameCaptureDiagnosticTap("gtao-history-samples") ==
				PostProcessDebugTap::GTAOHistorySamples &&
				!FindFrameCaptureDiagnosticTap("bloom-result") &&
				GetFrameCaptureDiagnosticTapName(PostProcessDebugTap::BloomResult).empty(),
				"Diagnostic taps have stable names; non-diagnostic taps have none");

			CaptureTestDevice device;
			FrameCaptureService service(device);
			const RHITextureDesc target = MakeDisplayTarget(RHIFormat::R8G8B8A8Unorm, 4, 2);
			const uint64_t unsupported = service.RequestDiagnosticCapture(PostProcessDebugTap::BloomResult);
			const std::vector<FrameCaptureResult> rejected = Consume(service);
			context.Check(rejected.size() == 1 && rejected[0].m_RequestId == unsupported &&
				rejected[0].m_Status == FrameCaptureStatus::Failed &&
				rejected[0].m_Source == FrameCaptureSource::Diagnostic,
				"A tap without diagnostic support fails immediately");

			const uint64_t weightA = service.RequestDiagnosticCapture(PostProcessDebugTap::TemporalHistoryWeight);
			const uint64_t rejection = service.RequestDiagnosticCapture(PostProcessDebugTap::TemporalRejection);
			const uint64_t weightB = service.RequestDiagnosticCapture(PostProcessDebugTap::TemporalHistoryWeight);
			context.Check(service.GetPendingDiagnosticTap() == PostProcessDebugTap::TemporalHistoryWeight &&
				service.HasPendingRequests(FrameCaptureSource::Diagnostic) &&
				!service.HasPendingRequests(FrameCaptureSource::Scene),
				"The pending diagnostic tap is the tap of the oldest diagnostic request");

			const std::optional<FrameCaptureTapTarget> weightTap =
				service.BindTap(3, FrameCaptureSource::Diagnostic, target);
			context.Check(weightTap &&
				service.GetPendingDiagnosticTap() == PostProcessDebugTap::TemporalRejection &&
				service.GetUnfinishedRequestCount() == 3,
				"A diagnostic tap binds only the requests for the pending tap");

			service.OnFrameAborted(3);
			context.Check(service.GetPendingDiagnosticTap() == PostProcessDebugTap::TemporalHistoryWeight,
				"Requests of an aborted frame return to the queue with their diagnostic tap");

			service.FailPendingDiagnosticRequests("No source.");
			const std::vector<FrameCaptureResult> failed = Consume(service);
			context.Check(failed.size() == 2 && failed[0].m_RequestId == weightA &&
				failed[1].m_RequestId == weightB &&
				failed[0].m_Status == FrameCaptureStatus::Failed &&
				failed[0].m_Failure == "No source." &&
				service.GetPendingDiagnosticTap() == PostProcessDebugTap::TemporalRejection,
				"Failing the pending tap finishes only its requests");

			const std::optional<FrameCaptureTapTarget> rejectionTap =
				service.BindTap(4, FrameCaptureSource::Diagnostic, target);
			if (rejectionTap)
			{
				device.FillReadback(rejectionTap->m_Buffer, rejectionTap->m_Footprint);
				service.OnFrameSubmitted(4, MakeFence(1));
				device.m_CompletedFenceValue = 1;
				service.CollectCompleted();
			}
			const std::vector<FrameCaptureResult> completed = Consume(service);
			context.Check(rejectionTap && completed.size() == 1 &&
				completed[0].m_RequestId == rejection &&
				completed[0].m_Status == FrameCaptureStatus::Completed &&
				completed[0].m_Source == FrameCaptureSource::Diagnostic &&
				!service.GetPendingDiagnosticTap(),
				"A bound diagnostic tap completes like any other capture");
			service.Shutdown();
		}

		void RunFrameBindingTests(SelfTestContext& context) noexcept
		{
			CaptureTestDevice device;
			FrameCaptureService service(device);
			const RHITextureDesc target = MakeDisplayTarget(RHIFormat::R8G8B8A8Unorm, 4, 2);
			const auto weight = PostProcessDebugTap::TemporalHistoryWeight;
			const auto rejection = PostProcessDebugTap::TemporalRejection;
			const uint64_t sceneA = service.RequestCapture(FrameCaptureSource::Scene, 100);
			const uint64_t sceneB = service.RequestCapture(FrameCaptureSource::Scene, 100);
			const uint64_t weightA = service.RequestDiagnosticCapture(weight, 100);
			const uint64_t rejected = service.RequestDiagnosticCapture(rejection, 100);
			const uint64_t weightB = service.RequestDiagnosticCapture(weight, 100);
			context.Check(!service.BindTap(99, FrameCaptureSource::Scene, target) &&
				device.GetLiveBufferCount() == 0,
				"A frame-restricted request cannot allocate or bind in another frame");
			const auto sceneTap = service.BindTap(100, FrameCaptureSource::Scene, target);
			const auto diagnosticTap = service.BindTap(100, FrameCaptureSource::Diagnostic, target);
			if (sceneTap)
			{
				device.FillReadback(sceneTap->m_Buffer, sceneTap->m_Footprint);
			}
			if (diagnosticTap)
			{
				device.FillReadback(diagnosticTap->m_Buffer, diagnosticTap->m_Footprint);
			}
			service.OnFrameSubmitted(100, MakeFence(1));
			const auto missed = Consume(service);
			context.Check(sceneTap && diagnosticTap && missed.size() == 1 &&
				missed[0].m_RequestId == rejected && missed[0].m_Status == FrameCaptureStatus::Failed &&
				missed[0].m_DiagnosticTap == rejection && !service.GetPendingDiagnosticTap() &&
				service.GetUnfinishedRequestCount() == 4,
				"A conflicting frame-restricted tap fails at submission rather than moving to another frame");
			context.Check(device.GetLiveBufferCount() == 2 && Consume(service).empty(),
				"Frame identity does not bypass the GPU completion fence");
			device.m_CompletedFenceValue = 1;
			const auto completed = Consume(service);
			const auto find = [&](uint64_t id)
				{
					return std::ranges::find(completed, id, &FrameCaptureResult::m_RequestId);
				};
			const auto a = find(sceneA), b = find(sceneB), c = find(weightA), d = find(weightB);
			context.Check(completed.size() == 4 && a != completed.end() && b != completed.end() &&
				c != completed.end() && d != completed.end() && a->m_Image == b->m_Image &&
				c->m_Image == d->m_Image && a->m_Image != c->m_Image && !a->m_DiagnosticTap &&
				c->m_DiagnosticTap == weight && a->m_FrameSerial == 100 && c->m_FrameSerial == 100,
				"Scene and diagnostic taps share images only within their actual frame and source");

			const uint64_t boundAbort = service.RequestDiagnosticCapture(weight, 101);
			const uint64_t flexible = service.RequestDiagnosticCapture(weight);
			const uint64_t queuedAbort = service.RequestCapture(FrameCaptureSource::Scene, 101);
			const auto abortedTap = service.BindTap(101, FrameCaptureSource::Diagnostic, target);
			service.OnFrameAborted(101);
			const auto aborted = Consume(service);
			context.Check(abortedTap && aborted.size() == 2 &&
				std::ranges::any_of(aborted, [&](const auto& r) { return r.m_RequestId == boundAbort; }) &&
				std::ranges::any_of(aborted, [&](const auto& r) { return r.m_RequestId == queuedAbort; }) &&
				service.GetUnfinishedRequestCount() == 1 &&
				!device.IsAlive(abortedTap->m_Buffer),
				"Abort fails both bound and unbound frame-restricted requests and requeues only flexible requests");
			const uint64_t retry = service.RequestDiagnosticCapture(weight, 102);
			const auto retryTap = service.BindTap(102, FrameCaptureSource::Diagnostic, target);
			if (retryTap)
			{
				device.FillReadback(retryTap->m_Buffer, retryTap->m_Footprint);
			}
			service.OnFrameSubmitted(102, MakeFence(2));
			device.m_CompletedFenceValue = 2;
			const auto retried = Consume(service);
			context.Check(retried.size() == 2 && retried[0].m_RequestId == flexible &&
				retried[1].m_RequestId == retry && retried[0].m_FrameSerial == 102 &&
				retried[1].m_FrameSerial == 102 && retried[0].m_DiagnosticTap == weight,
				"An explicit retry has a new frame identity; flexible requests keep their original queue order");

			GGLAB_UNUSED(service.RequestCapture(FrameCaptureSource::Scene, 103));
			GGLAB_UNUSED(service.RequestDiagnosticCapture(weight, 103));
			GGLAB_UNUSED(service.BindTap(103, FrameCaptureSource::Scene, target));
			service.OnFrameSubmissionFailed(103);
			const auto submissionFailed = Consume(service);
			context.Check(submissionFailed.size() == 2 && device.GetLiveBufferCount() == 0 &&
				service.GetUnfinishedRequestCount() == 0 &&
				std::ranges::all_of(submissionFailed, [](const auto& r) {
					return r.m_Status == FrameCaptureStatus::Failed && !r.m_Image;
				}), "Submission failure closes bound and unbound frame-restricted requests");
			GGLAB_UNUSED(service.RequestCapture(FrameCaptureSource::Composited, 104));
			service.OnFrameSubmitted(104, MakeFence(3));
			context.Check(Consume(service).size() == 1 &&
				!service.BindTap(105, FrameCaptureSource::Composited, target),
				"A frame without its source cannot leave a restricted capture pending for a later frame");
			service.Shutdown();
			context.Check(Consume(service).empty(), "Frame-restricted results are published exactly once");
		}

		void RunFrameEndTests(SelfTestContext& context) noexcept
		{
			CaptureTestDevice device;
			FrameCaptureService service(device);
			const RHITextureDesc target = MakeDisplayTarget(RHIFormat::R8G8B8A8Unorm, 4, 4);

			const uint64_t first = service.RequestCapture(FrameCaptureSource::Composited);
			const std::optional<FrameCaptureTapTarget> abortedTap =
				service.BindTap(1, FrameCaptureSource::Composited, target);
			const uint64_t second = service.RequestCapture(FrameCaptureSource::Composited);
			service.OnFrameAborted(1);
			context.Check(abortedTap && !device.IsAlive(abortedTap->m_Buffer) &&
				Consume(service).empty() && service.GetUnfinishedRequestCount() == 2 &&
				service.HasPendingRequests(FrameCaptureSource::Composited),
				"An aborted frame returns its requests to the queue and releases the readback");

			const std::optional<FrameCaptureTapTarget> retryTap =
				service.BindTap(2, FrameCaptureSource::Composited, target);
			service.OnFrameSubmissionFailed(2);
			const std::vector<FrameCaptureResult> failed = Consume(service);
			context.Check(retryTap && failed.size() == 2 && failed[0].m_RequestId == first &&
				failed[1].m_RequestId == second &&
				failed[0].m_Status == FrameCaptureStatus::Failed && failed[0].m_FrameSerial == 2 &&
				!device.IsAlive(retryTap->m_Buffer) && service.GetUnfinishedRequestCount() == 0,
				"Re-queued requests keep their order and fail with the frame that failed to submit");

			const uint64_t noCopy = service.RequestCapture(FrameCaptureSource::Scene);
			const bool noCopyBound = service.BindTap(3, FrameCaptureSource::Scene,
				MakeDisplayTarget(RHIFormat::R8G8B8A8Unorm, 4, 4, false)).has_value();
			const uint64_t noFootprint = service.RequestCapture(FrameCaptureSource::Scene);
			const bool noFootprintBound = service.BindTap(3, FrameCaptureSource::Scene,
				MakeDisplayTarget(RHIFormat::D32Float, 4, 4)).has_value();
			device.m_FailBufferCreation = true;
			const uint64_t noBuffer = service.RequestCapture(FrameCaptureSource::Scene);
			const bool noBufferBound =
				service.BindTap(3, FrameCaptureSource::Scene, target).has_value();
			device.m_FailBufferCreation = false;
			const std::vector<FrameCaptureResult> rejected = Consume(service);
			context.Check(!noCopyBound && !noFootprintBound && !noBufferBound &&
				rejected.size() == 3 && rejected[0].m_RequestId == noCopy &&
				rejected[1].m_RequestId == noFootprint && rejected[2].m_RequestId == noBuffer &&
				rejected[0].m_Status == FrameCaptureStatus::Failed &&
				rejected[0].m_FrameSerial == 0 && service.GetUnfinishedRequestCount() == 0,
				"Display targets without copy support, footprint or readback memory fail at bind");

			const uint64_t unmapped = service.RequestCapture(FrameCaptureSource::Scene);
			const std::optional<FrameCaptureTapTarget> unmappedTap =
				service.BindTap(4, FrameCaptureSource::Scene, target);
			service.OnFrameSubmitted(4, MakeFence(9));
			device.m_CompletedFenceValue = 9;
			device.m_FailMapping = true;
			service.CollectCompleted();
			device.m_FailMapping = false;
			const std::vector<FrameCaptureResult> mapFailure = Consume(service);
			context.Check(unmappedTap && mapFailure.size() == 1 &&
				mapFailure[0].m_RequestId == unmapped &&
				mapFailure[0].m_Status == FrameCaptureStatus::Failed &&
				mapFailure[0].m_FrameSerial == 4 && !device.IsAlive(unmappedTap->m_Buffer),
				"A completed readback that cannot be mapped fails and is released");

			const uint64_t inFlight = service.RequestCapture(FrameCaptureSource::Scene);
			const std::optional<FrameCaptureTapTarget> inFlightTap =
				service.BindTap(5, FrameCaptureSource::Scene, target);
			service.OnFrameSubmitted(5, MakeFence(12));
			service.Shutdown();
			const std::vector<FrameCaptureResult> shutdown = Consume(service);
			context.Check(inFlightTap && shutdown.size() == 1 &&
				shutdown[0].m_RequestId == inFlight &&
				shutdown[0].m_Status == FrameCaptureStatus::Failed &&
				shutdown[0].m_FrameSerial == 5 && device.GetLiveBufferCount() == 0,
				"Shutdown fails submitted taps whose fence never completed and releases them");
		}

		void RunCapacityTests(SelfTestContext& context) noexcept
		{
			CaptureTestDevice device;
			const RHITextureDesc target = MakeDisplayTarget(RHIFormat::R8G8B8A8Unorm, 4, 4);
			// Exactly one pitched GPU buffer plus its tight CPU copy fits. A retained
			// CPU image must still prevent another copy even after ConsumeResults.
			FrameCaptureService service(device, { .m_MaxRequests = 4, .m_MaxPayloads = 2,
				.m_MaxPayloadBytes = 1088 });
			const auto availability = [&]() { return service.GetAvailability(target.m_Format, 4, 4); };
			context.Check(availability() == FrameCaptureAvailability::Available &&
				service.GetAvailability(target.m_Format, 8, 4) == FrameCaptureAvailability::TooLarge &&
				service.GetAvailability(target.m_Format, std::numeric_limits<uint32_t>::max(),
					std::numeric_limits<uint32_t>::max()) == FrameCaptureAvailability::TooLarge &&
				service.GetAvailability(RHIFormat::D32Float, 4, 4) == FrameCaptureAvailability::Unsupported,
				"Capacity includes row pitch and the CPU copy, and rejects impossible or overflowing targets");
			const uint64_t first = service.RequestCapture(FrameCaptureSource::Scene);
			const uint64_t shared = service.RequestCapture(FrameCaptureSource::Scene);
			const auto tap = service.BindTap(1, FrameCaptureSource::Scene, target);
			service.OnFrameSubmitted(1, MakeFence(1));
			context.Check(tap && availability() == FrameCaptureAvailability::Busy &&
				device.GetLiveBufferCount() == 1 && Consume(service).empty(),
				"An incomplete fence holds the full readback and CPU-copy reservation without mapping");
			GGLAB_UNUSED(service.RequestCapture(FrameCaptureSource::Composited));
			const auto overflow = service.BindTap(2, FrameCaptureSource::Composited, target);
			auto rejected = Consume(service);
			context.Check(!overflow && rejected.size() == 1 &&
				rejected[0].m_Status == FrameCaptureStatus::Failed && rejected[0].m_FrameSerial == 0 &&
				device.GetLiveBufferCount() == 1,
				"An exhausted byte budget fails the requested frame before allocating another readback");
			device.m_CompletedFenceValue = 1;
			// No BeginFrame or explicit CollectCompleted: polling must drain a paused renderer.
			auto results = Consume(service);
			const bool completed = results.size() == 2 && results[0].m_RequestId == first &&
				results[1].m_RequestId == shared && results[0].m_Image &&
				results[0].m_Image == results[1].m_Image;
			context.Check(completed && device.GetLiveBufferCount() == 0 &&
				availability() == FrameCaptureAvailability::Busy,
				"Result polling retires completed GPU work while consumers still reserve their shared CPU image");
			std::shared_ptr<const FrameCaptureImage> held = completed ? results[0].m_Image : nullptr;
			results.clear();
			context.Check(held && availability() == FrameCaptureAvailability::Busy,
				"Moving an image out of the result queue does not release its payload byte budget");
			held.reset();
			context.Check(availability() == FrameCaptureAvailability::Available,
				"The last CPU image consumer releases the byte reservation without a new render frame");
			GGLAB_UNUSED(service.RequestCapture(FrameCaptureSource::Scene));
			const auto aborted = service.BindTap(3, FrameCaptureSource::Scene, target);
			service.OnFrameAborted(3);
			const auto retried = service.BindTap(4, FrameCaptureSource::Scene, target);
			service.OnFrameSubmissionFailed(4);
			context.Check(aborted && retried && device.GetLiveBufferCount() == 0 &&
				Consume(service).size() == 1 && availability() == FrameCaptureAvailability::Available,
				"Abort returns the request and its reservation; submission failure releases retry capacity");
			service.Shutdown();
			context.Check(availability() == FrameCaptureAvailability::Finalized,
				"Shutdown reports permanent unavailability rather than temporary pressure");

			FrameCaptureService countLimited(device, { .m_MaxRequests = 3, .m_MaxPayloads = 1,
				.m_MaxPayloadBytes = 4096 });
			GGLAB_UNUSED(countLimited.RequestCapture(FrameCaptureSource::Scene));
			GGLAB_UNUSED(countLimited.RequestCapture(FrameCaptureSource::Composited));
			GGLAB_UNUSED(countLimited.RequestDiagnosticCapture(PostProcessDebugTap::TemporalHistoryWeight));
			context.Check(countLimited.RequestCapture(FrameCaptureSource::Scene) == 0 &&
				countLimited.RequestDiagnosticCapture(PostProcessDebugTap::TemporalHistoryWeight) == 0 &&
				countLimited.GetRequestAvailability() == FrameCaptureAvailability::Busy &&
				Consume(countLimited).empty() && countLimited.GetUnfinishedRequestCount() == 3,
				"Full request slots reject scene and diagnostic admission without allocating a result or id");
			const auto single = countLimited.BindTap(5, FrameCaptureSource::Scene, target);
			const auto second = countLimited.BindTap(5, FrameCaptureSource::Composited, target);
			context.Check(single && !second && device.GetLiveBufferCount() == 1 &&
				countLimited.RequestCapture(FrameCaptureSource::Scene) == 0,
				"Payload count limits distinct taps and unconsumed failures continue occupying request slots");
			GGLAB_UNUSED(Consume(countLimited));
			countLimited.OnFrameSubmissionFailed(5);
			GGLAB_UNUSED(Consume(countLimited));
			const auto diagnostic = countLimited.BindTap(6, FrameCaptureSource::Diagnostic, target);
			countLimited.OnFrameSubmitted(6, MakeFence(6));
			device.m_CompletedFenceValue = 6;
			auto imageResult = Consume(countLimited);
			context.Check(diagnostic && imageResult.size() == 1 &&
				countLimited.GetAvailability(target.m_Format, 4, 4) == FrameCaptureAvailability::Busy,
				"A retained CPU image holds its payload slot even when ample byte budget remains");
			imageResult.clear();
			context.Check(countLimited.GetAvailability(target.m_Format, 4, 4) == FrameCaptureAvailability::Available,
				"Consuming failures and releasing CPU images restores both admission limits");
			device.m_FailBufferCreation = true;
			GGLAB_UNUSED(countLimited.RequestCapture(FrameCaptureSource::Scene));
			const auto failedAllocation = countLimited.BindTap(7, FrameCaptureSource::Scene, target);
			device.m_FailBufferCreation = false;
			context.Check(!failedAllocation && Consume(countLimited).size() == 1 &&
				countLimited.GetAvailability(target.m_Format, 4, 4) == FrameCaptureAvailability::Available,
				"Allocation failure leaves no payload reservation behind");
			GGLAB_UNUSED(countLimited.RequestCapture(FrameCaptureSource::Scene));
			const auto failedMapping = countLimited.BindTap(8, FrameCaptureSource::Scene, target);
			countLimited.OnFrameSubmitted(8, MakeFence(8));
			device.m_CompletedFenceValue = 8;
			device.m_FailMapping = true;
			const auto mapFailure = Consume(countLimited);
			device.m_FailMapping = false;
			context.Check(failedMapping && mapFailure.size() == 1 &&
				mapFailure[0].m_Status == FrameCaptureStatus::Failed && device.GetLiveBufferCount() == 0 &&
				countLimited.GetAvailability(target.m_Format, 4, 4) == FrameCaptureAvailability::Available,
				"Mapping failure releases the single payload slot and byte reservation");
			countLimited.Shutdown();
		}

		void RunGraphCaptureTests(SelfTestContext& context) noexcept
		{
			CaptureTestDevice device;
			TransientResourcePool pool(&device);
			FrameCaptureService service(device);
			RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
			context.Check(graph.Compile(), "Empty graph capture fixture compiles");
			const uint64_t id = service.RequestCapture(FrameCaptureSource::RenderGraph, 7);
			service.BeginGraphRecording(6, graph);
			context.Check(service.GetUnfinishedRequestCount() == 1,
				"Graph recording cannot retarget a frame-restricted request");
			service.BeginGraphRecording(7, graph);
			RGExecuteContext executeContext({});
			graph.Execute(executeContext);
			service.EndGraphRecording(7, graph);
			service.OnFrameSubmitted(7, MakeFence(9));
			context.Check(Consume(service).empty() && device.GetLiveBufferCount() == 0 && device.GetMapCount() == 0,
				"CPU graph records still wait for the selected submission without GPU readback");
			device.m_CompletedFenceValue = 9;
			const auto results = Consume(service);
			context.Check(results.size() == 1 && results[0].m_RequestId == id && results[0].m_Graph &&
				results[0].m_Graph->m_Execution.m_Recorded && !results[0].m_Image &&
				results[0].m_FrameSerial == 7 && results[0].m_SubmissionFenceValue == 9 &&
				results[0].m_SubmissionFenceIndex == 1 &&
				results[0].m_AdapterCompatibilityIdentity == "GGLab.FrameCaptureTestDevice" && Consume(service).empty(),
				"Graph results preserve frame and fence identity and publish exactly once");
			const uint64_t aborted = service.RequestCapture(FrameCaptureSource::RenderGraph, 8);
			const uint64_t flexible = service.RequestCapture(FrameCaptureSource::RenderGraph);
			service.BeginGraphRecording(8, graph);
			service.OnFrameAborted(8);
			const auto abortResults = Consume(service);
			context.Check(abortResults.size() == 1 && abortResults[0].m_RequestId == aborted &&
				service.GetUnfinishedRequestCount() == 1,
				"Graph abort fails exact requests and requeues flexible requests");
			service.BeginGraphRecording(9, graph);
			service.OnFrameSubmissionFailed(9);
			const auto failed = Consume(service);
			context.Check(failed.size() == 1 && failed[0].m_RequestId == flexible && !failed[0].m_Graph,
				"Failed graph submissions do not publish execution evidence");
			service.Shutdown();

			RenderGraph duplicates({ .m_Device = &device, .m_TransientResourcePool = &pool });
			duplicates.AddTrivialSideEffectPass("Repeated", [](RGExecuteContext&) {});
			duplicates.AddTrivialSideEffectPass("Repeated", [](RGExecuteContext&) {});
			context.Check(duplicates.Compile(), "Duplicate graph pass names are legal");
			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(duplicates, snapshot);
			using Json = nlohmann::json;
			const Json full = Json::parse(SerializeRenderGraphSnapshot(snapshot));
			const Json summary = Json::parse(SerializeRenderGraphSnapshot(snapshot, true));
			context.Check(full["schema"] == "gglab.render-graph" && full["schemaVersion"] == 1 &&
				full["compile"]["passes"][0]["occurrence"] == 1 &&
				full["compile"]["passes"][1]["occurrence"] == 2 &&
				full["compile"]["duplicatePassNames"] == Json::array({"Repeated"}) &&
				full["execution"]["gpuTiming"]["status"] == "unavailable" &&
				!full["execution"]["gpuTiming"]["reason"].get<std::string>().empty() &&
				summary["compile"]["passes"].size() == 2 && !summary.contains("execution"),
				"Graph JSON separates compile/execution and never guesses merged duplicate timings");
		}

		template <typename E>
		void CheckGraphEnumIds(SelfTestContext& context, std::string_view expected) noexcept
		{
			std::string ids;
			for (const auto& entry : diagnostics::RGEnumTextTraits<E>::Entries)
			{
				if (!ids.empty())
				{
					ids += ',';
				}
				ids += diagnostics::RGEnumId(entry.m_Value);
			}
			context.Check(ids == expected, "Graph schema enum identifiers are pinned independently of UI labels");
		}

		void RunGraphEnumTests(SelfTestContext& context) noexcept
		{
			CheckGraphEnumIds<RGPassEncoderType>(context, "Graphics,Compute,Copy");
			CheckGraphEnumIds<RGResourceType>(context, "Texture,Buffer");
			CheckGraphEnumIds<RGOrderingRequirement>(context, "Ordered,Unordered");
			CheckGraphEnumIds<RGDependencyAccess>(context, "Read,Write,ReadWrite");
			CheckGraphEnumIds<RGDependencyReason>(context,
				"WriterToReader,PreviousWriterToWriter,PreviousReaderToWriter,ExportWriterToExport,ExportReaderToExport,ObservationOrder");
			CheckGraphEnumIds<RGBarrierKind>(context, "Transition,Uav");
			CheckGraphEnumIds<RGBarrierReason>(context, "AccessTransition,OrderedStorageHazard,FinalStateTransition");
			CheckGraphEnumIds<RGTextureAccess>(context,
				"None,Sample,RenderTarget,DepthStencilWrite,DepthStencilRead,StorageRead,StorageWrite,StorageReadWrite,CopySource,CopyDest,Present");
			CheckGraphEnumIds<RGBufferAccess>(context,
				"None,Vertex,Index,Constant,StructuredRead,StorageRead,StorageWrite,StorageReadWrite,CopySource,CopyDest,IndirectArgument");
			CheckGraphEnumIds<RHITextureUsage>(context,
				"Sampled,RenderTarget,DepthStencil,UnorderedAccess,CopySource,CopyDest,Present");
			CheckGraphEnumIds<RHIBufferUsage>(context,
				"Vertex,Index,Constant,Structured,UnorderedAccess,IndirectArgument,CopySource,CopyDest");
			CheckGraphEnumIds<RHIStage>(context,
				"DrawIndirect,IndexInput,VertexShader,PixelShader,ComputeShader,RenderTarget,DepthStencil,Copy,Resolve,Present");
			CheckGraphEnumIds<RHITextureAspect>(context, "Color,Depth,Stencil");
			CheckGraphEnumIds<RHIAccess>(context,
				"Common,ShaderResource,RenderTarget,DepthStencilRead,DepthStencilWrite,UnorderedAccess,CopySource,CopyDest,VertexBuffer,IndexBuffer,ConstantBuffer,IndirectArgument,Present");
			CheckGraphEnumIds<RHILayout>(context,
				"Unknown,Common,ShaderResource,RenderTarget,DepthStencilRead,DepthStencilWrite,UnorderedAccess,CopySource,CopyDest,Present,Undefined");
			context.Check(diagnostics::RGFlagsId(RHIStage::VertexShader | RHIStage::PixelShader) ==
				"VertexShader | PixelShader" && diagnostics::RGFlagsId(RHIStage::None) == "None",
				"Graph flag identifiers retain their schema separator and zero representation");
		}

		void RunGraphVersionTests(SelfTestContext& context) noexcept
		{
			CaptureTestDevice device;
			TransientResourcePool pool(&device);
			RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
			struct Data {};
			RGTextureId texture;
			RGBufferId buffer;
			const RHISubresourceRange remaining{};
			graph.AddPass<Data>("Repeated", [](RenderGraph::RGBuilder&, Data&) {});
			graph.AddPass<Data>("Initialize", [&](RenderGraph::RGBuilder& builder, Data&)
				{
					texture = builder.CreateTexture("SceneColor", { .m_Format = RHIFormat::R8G8B8A8Unorm,
						.m_Extent = { 4, 4, 1 } });
					buffer = builder.CreateBuffer("Structured", { .m_SizeInBytes = 65536, .m_StrideInBytes = 32 });
					builder.WriteInPlace(texture, RGTextureAccess::RenderTarget);
					builder.WriteInPlace(buffer, RGBufferAccess::StorageWrite);
				});
			for (uint32_t i = 0; i < 2; ++i)
			{
				graph.AddPass<Data>("Repeated", [&](RenderGraph::RGBuilder& builder, Data&)
					{
						builder.ReadWriteInPlace(texture, RGTextureAccess::RenderTarget, remaining);
						builder.ReadWriteInPlace(buffer, RGBufferAccess::StorageReadWrite);
					});
			}
			graph.AddPass<Data>("Consume", [&](RenderGraph::RGBuilder& builder, Data&)
				{
					GGLAB_UNUSED(builder.Read(texture, RGTextureAccess::Sample));
					GGLAB_UNUSED(builder.Read(buffer, RGBufferAccess::StructuredRead));
					builder.SideEffect();
				});
			context.Check(graph.Compile(), "ReadWrite graph discovery fixture compiles");
			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(graph, snapshot);
			using Json = nlohmann::json;
			const Json full = Json::parse(SerializeRenderGraphSnapshot(snapshot));
			const Json summary = Json::parse(SerializeRenderGraphSnapshot(snapshot, true));
			context.Check(full["compile"]["passes"][0]["culled"] == true &&
				summary["compile"]["passes"].size() == 4 &&
				summary["compile"]["passes"][1]["occurrence"] == 2 &&
				summary["compile"]["passes"][2]["occurrence"] == 3,
				"Summary occurrences retain declaration order including culled passes");
			for (const auto& pass : summary["compile"]["passes"])
			{
				const uint32_t index = pass["index"];
				for (const auto& write : pass["writes"])
				{
					const auto& node = full["compile"]["versions"][write["resourceNodeIndex"].get<uint32_t>()];
					context.Check(node["writer"] == index && node["version"] == write["version"],
						"Every summary write names the version whose writer is that pass");
				}
				for (size_t i = 0; i < pass["reads"].size(); ++i)
				{
					const auto& access = full["compile"]["passes"][index]["accesses"][i];
					context.Check(pass["reads"][i]["version"] == access["version"] &&
						pass["reads"][i]["resourceNodeIndex"] == access["resourceNodeIndex"],
						"Summary reads preserve the incoming version");
					if (access["dependencyAccess"] == "ReadWrite")
					{
						context.Check(access["writtenVersion"].get<uint32_t>() == access["version"].get<uint32_t>() + 1 &&
							access["writtenVersion"] == pass["writes"][i]["version"] &&
							access["writtenResourceNodeIndex"] == pass["writes"][i]["resourceNodeIndex"],
							"Full ReadWrite accesses distinguish consumed and produced versions");
					}
					else
					{
						context.Check(access["writtenVersion"].is_null() && access["writtenResourceNodeIndex"].is_null(),
							"Read-only accesses have no written version");
					}
				}
			}
			const auto& range = full["compile"]["passes"][2]["accesses"][0]["subresources"];
			context.Check(range["mipCount"] == "remaining" && range["sliceCount"] == "remaining" &&
				full["compile"]["resources"][1]["buffer"]["sizeBytes"] == 65536 &&
				full["compile"]["resources"][1]["buffer"]["strideBytes"] == 32 &&
				full["compile"]["passes"][2]["accesses"][0]["access"] == "RenderTarget" &&
				full["compile"]["dependencyEdges"][0]["reason"] == "WriterToReader",
				"Full JSON preserves buffer descriptors, stable enum ids and readable remaining counts");

			RenderGraph invalid({ .m_Device = &device, .m_TransientResourcePool = &pool });
			invalid.AddPass<Data>("ReadUndefined", [](RenderGraph::RGBuilder& builder, Data&)
				{
					const auto input = builder.CreateBuffer("Undefined");
					GGLAB_UNUSED(builder.Read(input, RGBufferAccess::StructuredRead));
					builder.SideEffect();
				});
			context.Check(!invalid.Compile(), "Undefined-read graph fixture fails compilation");
			BuildRenderGraphSnapshot(invalid, snapshot);
			const Json failed = Json::parse(SerializeRenderGraphSnapshot(snapshot));
			const auto& diagnostic = failed["compile"]["diagnostics"][0];
			context.Check(diagnostic["severity"] == "Error" && diagnostic["code"] == "UndefinedContentRead" &&
				diagnostic["passIndex"] == 0 && diagnostic["resourceIndex"] == 0 &&
				!diagnostic["message"].get<std::string>().empty() && failed["execution"]["status"] == "unavailable",
				"Generic compile-only JSON retains diagnostics without claiming execution");
		}

		void RunResourceExportTests(SelfTestContext& context) noexcept
		{
			CaptureTestDevice device;
			TransientResourcePool pool(&device);
			FrameCaptureService service(device);
			RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
			struct Data { RGBufferId m_Buffer; };
			RGBufferId buffer;
			graph.AddPass<Data>("Repeated", RGPassEncoderType::Copy, [](auto&, auto&) {});
			graph.AddPass<Data>("Initialize", RGPassEncoderType::Copy, [&](auto& builder, auto& data)
				{
					buffer = builder.CreateBuffer("ForwardPlus.TileLightHeaders", { .m_SizeInBytes = 16, .m_StrideInBytes = 8 });
					data.m_Buffer = builder.Write(buffer, RGBufferAccess::CopyDest);
					buffer = data.m_Buffer;
				}, [&](auto& execution, auto& data)
				{
					const auto handle = execution.GetBufferHandle(data.m_Buffer);
					auto* words = static_cast<uint32_t*>(device.MapBuffer(handle, {}));
					for (uint32_t i = 0; i < 4; ++i) words[i] = 10 + i;
					device.UnmapBuffer(handle, {});
				});
			graph.AddPass<Data>("Repeated", RGPassEncoderType::Copy, [&](auto& builder, auto& data)
				{ data.m_Buffer = builder.ReadWrite(buffer, RGBufferAccess::StorageReadWrite); buffer = data.m_Buffer; builder.SideEffect(); },
				[&](auto& execution, auto& data)
				{
					const auto handle = execution.GetBufferHandle(data.m_Buffer);
					auto* words = static_cast<uint32_t*>(device.MapBuffer(handle, {}));
					for (uint32_t i = 0; i < 4; ++i) words[i] = 20 + i;
					device.UnmapBuffer(handle, {});
				});
			context.Check(graph.Compile(), "Resource-version fixture compiles before observations");
			const RGResourceAddress before{ .m_Resource = "ForwardPlus.TileLightHeaders", .m_Position = RGObservationPosition::Before,
				.m_Pass = "Repeated", .m_Occurrence = 2 };
			const RGResourceAddress final{ .m_Resource = "ForwardPlus.TileLightHeaders" };
			auto ambiguous = before; ambiguous.m_Occurrence.reset();
			auto culled = before; culled.m_Occurrence = 1;
			context.Check(graph.ResolveObservation(ambiguous).m_Failure == RGObservationFailure::AmbiguousPass &&
				graph.ResolveObservation(culled).m_Failure == RGObservationFailure::Culled,
				"Observation occurrence counts include culled declarations and never revive them");
			const auto id = service.RequestResourceExport({ before, final }, 7, false, true);
			context.Check(service.PrepareResourceExports(7, graph) == FrameCaptureAvailability::Available,
				"A complete resource group reserves its payloads together");
			RGSnapshot snapshot; BuildRenderGraphSnapshot(graph, snapshot);
			const auto json = nlohmann::json::parse(SerializeRenderGraphSnapshot(snapshot));
			context.Check(json["schemaVersion"] == 2 && json["compile"]["passes"][0]["culled"] == true &&
				json["compile"]["passes"][1]["executionOrder"] == 0 && json["compile"]["passes"][2]["executionOrder"] == 2,
				"Observers preserve original liveness/order and occupy the exact version cut");
			service.BeginGraphRecording(7, graph);
			ExportTestCommands commands(device);
			RGExecuteContext execution({ .m_GraphicsCommandContext = &commands });
			graph.Execute(execution);
			service.EndGraphRecording(7, graph);
			service.OnFrameSubmitted(7, MakeFence(3));
			service.CollectCompleted();
			std::vector<FrameCaptureResult> results; service.ConsumeResults(results);
			context.Check(results.empty() && device.GetMapCount() == 0, "Resource payloads wait for their submission fence");
			device.m_CompletedFenceValue = 3; service.CollectCompleted(); service.ConsumeResults(results);
			context.Check(results.size() == 1 && results[0].m_RequestId == id && results[0].m_Resources && results[0].m_Graph &&
				results[0].m_Graph->m_Execution.m_Recorded, "Resource arrays and their optional graph publish once with the same completed frame");
			if (!results.empty() && results[0].m_Resources)
			{
				const auto& arrays = results[0].m_Resources->m_Arrays;
				uint32_t first = 0, last = 0;
				std::memcpy(&first, arrays[0].m_Data.data(), 4); std::memcpy(&last, arrays[1].m_Data.data(), 4);
				context.Check(first == 10 && last == 20 && arrays[0].m_Fields.size() == 2 && arrays[0].m_Stride == 8 &&
					arrays[0].m_Shape == std::vector<uint64_t>{2}, "An overwritten physical buffer exports both exact logical versions and its registered layout");
				ForwardPlusDebugReadback debugReadback;
				debugReadback.Initialize(device, 1);
				const auto grid = MakeForwardPlusTileGrid(32, 16);
				debugReadback.PrepareGridBuffer(device, 0, grid);
				const auto handle = debugReadback.GetGridBuffer(0);
				auto* mapped = device.MapBuffer(handle, {});
				std::memcpy(mapped, arrays[1].m_Data.data(), arrays[1].m_Data.size());
				device.UnmapBuffer(handle, {});
				debugReadback.MarkGridScheduled(0, 7, grid); debugReadback.ConsumeCompletedSlot(0);
				const auto readback = debugReadback.GetLatestGrid();
				context.Check(readback && readback->m_IsValid && readback->m_Headers.size() == 2 &&
					std::memcmp(readback->m_Headers.data(), arrays[1].m_Data.data(), arrays[1].m_Data.size()) == 0,
					"Resource-export tile-header bytes agree with the existing ForwardPlusDebugReadback decoder");
			}
			graph.Retire(MakeFence(3));

			RenderGraph partial({ .m_Device = &device, .m_TransientResourcePool = &pool });
			struct Empty {};
			partial.AddPass<Empty>("Partial", RGPassEncoderType::Copy, [](auto& builder, auto&)
				{
					auto texture = builder.CreateTexture("Partial", { .m_Format = RHIFormat::R8Unorm,
						.m_Extent = {4, 4, 1}, .m_ArraySize = 2, .m_MipLevels = 2 });
					builder.WriteInPlace(texture, RGTextureAccess::CopyDest, RHISubresourceRange{1, 1, 1, 1, RHITextureAspect::Color});
					builder.SideEffect();
				});
			context.Check(partial.Compile(), "Partial mip/slice fixture compiles");
			context.Check(partial.ResolveObservation({ .m_Resource = "Partial", .m_Mip = 1, .m_FirstSlice = 1, .m_SliceCount = 1 }).m_Failure == RGObservationFailure::None &&
				partial.ResolveObservation({ .m_Resource = "Partial" }).m_Failure == RGObservationFailure::UndefinedContent &&
				partial.ResolveObservation({ .m_Resource = "missing" }).m_Failure == RGObservationFailure::UnknownResource,
				"Only the defined mip and slice can be observed");

			FrameCaptureService bounded(device, { .m_MaxPayloads = 1, .m_MaxPayloadBytes = 1024 });
			const auto tooLarge = bounded.RequestResourceExport({ final, final }, 8);
			std::vector<FrameCaptureResult> failures; bounded.ConsumeResults(failures);
			context.Check(failures.size() == 1 && failures[0].m_RequestId == tooLarge && failures[0].m_ResourceFailure == RGObservationFailure::TooLarge,
				"Never-fitting resource groups fail immediately instead of being split");
			const auto imageId = bounded.RequestCapture(FrameCaptureSource::Scene, 8);
			const auto imageTap = bounded.BindTap(8, FrameCaptureSource::Scene, { .m_Format = RHIFormat::R8G8B8A8Unorm, .m_Usage = RHITextureUsage::CopySource, .m_Extent = {1, 1, 1} });
			context.Check(imageId != 0 && imageTap.has_value(), "Busy fixture occupies the shared payload slot");
			context.Check(bounded.GetResourceExportFrameAvailability() == FrameCaptureAvailability::Busy,
				"Scheduled exports wait for payload retirement before simulation begins");
			const auto waiting = bounded.RequestResourceExport({{ .m_Resource = "Partial", .m_Mip = 1, .m_FirstSlice = 1, .m_SliceCount = 1 }}, 9, true);
			const auto graphId = bounded.RequestCapture(FrameCaptureSource::RenderGraph, 9);
			context.Check(bounded.PrepareResourceExports(9, partial) == FrameCaptureAvailability::Busy && !partial.HasResourceObservations(),
				"Busy exact-sequence groups defer before graph extension or GPU submission");
			failures.clear(); bounded.ConsumeResults(failures);
			context.Check(failures.size() == 2 && failures[0].m_RequestId == waiting && failures[1].m_RequestId == graphId &&
				std::ranges::all_of(failures, [](const auto& result) { return result.m_Status == FrameCaptureStatus::Busy && result.m_FrameSerial == 9; }),
				"Every request on the deferred frame retries as one frame with fresh metadata");
			bounded.OnFrameAborted(8);
		}

		void RunNonFiniteDefaultTests(SelfTestContext& context) noexcept
		{
			CaptureTestDevice device;
			TransientResourcePool pool(&device);
			RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
			const auto unknown = ResolveNonFiniteWatchDefaults(graph);
			context.Check(std::ranges::all_of(unknown, [](const auto& target) { return target.m_Claim.m_Status == NonFiniteClaimStatus::Incomplete; }),
				"An unregistered pipeline cannot imply inactive features or complete coverage");
			auto& defaults = graph.GetBlackboard().GetOrCreate<NonFiniteWatchDefaultRegistry>();
			defaults.Declare({ true, false, true, true });
			RGTextureId texture;
			RGTextureId original;
			RGResourceAddress consumer;
			struct Data {};
			graph.AddPass<Data>("Repeated", [](auto&, auto&) {});
			graph.AddPass<Data>("Produce", [&](auto& builder, auto&)
				{
					texture = builder.CreateTexture("Renamed.FloatColor", { .m_Format = RHIFormat::R16G16B16A16Float, .m_Extent = { 4, 4, 1 } });
					builder.WriteInPlace(texture, RGTextureAccess::RenderTarget);
					original = texture;
					defaults.Register(NonFiniteWatchSemantic::MotionVectors, texture,
						builder.GetObservationAddress(texture, RGObservationPosition::After));
				});
			graph.AddPass<Data>("DeadConsumer", [&](auto& builder, auto&)
				{
					const auto input = builder.Read(texture, RGTextureAccess::Sample);
					defaults.Register(NonFiniteWatchSemantic::PostProcessInput, input, builder.GetObservationAddress(input));
				});
			graph.AddPass<Data>("Repeated", [&](auto& builder, auto&)
				{
					const auto input = builder.Read(texture, RGTextureAccess::Sample);
					consumer = builder.GetObservationAddress(input);
					defaults.Register(NonFiniteWatchSemantic::TemporalInput, input, consumer);
					defaults.Register(NonFiniteWatchSemantic::PostProcessInput, input, consumer);
					builder.SideEffect();
				});
			graph.AddPass<Data>("LateConsumer", [&](auto& builder, auto&)
				{
					const auto input = builder.Read(texture, RGTextureAccess::Sample);
					defaults.Register(NonFiniteWatchSemantic::PostProcessInput, input, builder.GetObservationAddress(input));
					builder.SideEffect();
				});
			graph.AddPass<Data>("Overwrite", [&](auto& builder, auto&)
				{
					builder.ReadWriteInPlace(texture, RGTextureAccess::RenderTarget);
					builder.SideEffect();
				});
			context.Check(graph.Compile(), "Semantic defaults fixture compiles before resolution");
			const auto resolved = ResolveNonFiniteWatchDefaults(graph);
			context.Check(resolved[0].m_Ready && resolved[0].m_Resource == original &&
				resolved[1].m_Claim.m_Status == NonFiniteClaimStatus::NotApplicable &&
				resolved[2].m_Ready && resolved[2].m_Resource == original && resolved[2].m_Resource != texture,
				"Semantic registration survives resource renames and binds the consumed version before a later overwrite");
			context.Check(consumer.m_Occurrence == 2u && resolved[3].m_Ready && resolved[3].m_Address.m_Pass == "Repeated",
				"Address generation counts culled declarations and defaults select the earliest live consumer");
			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(graph, snapshot);
			const auto json = nlohmann::json::parse(SerializeRenderGraphSnapshot(snapshot));
			context.Check(json["compile"]["passes"][2]["culled"] == true && !graph.HasNonFiniteScans(),
				"Registration and resolution neither revive dead consumers nor add scan work while watch is off");
			const auto target = graph.ResolveObservation(consumer, RGObservationKind::ResourceExport);
			context.Check(GetNonFiniteDefaultWatchTier(graph, target) == 1u &&
				!GetNonFiniteDefaultWatchTier(graph, graph.ResolveObservation({ .m_Resource = "Renamed.FloatColor" }, RGObservationKind::ResourceExport)),
				"Export tiers follow the registered version rather than the final resource name");
			NonFiniteWatchDefaultRegistry missing;
			missing.Declare({ true, false, false, false });
			context.Check(missing.Resolve(graph)[0].m_Claim.m_Status == NonFiniteClaimStatus::Incomplete,
				"An active feature with an omitted binding reports missing coverage");
			missing.Register(NonFiniteWatchSemantic::MotionVectors, original,
				{ .m_Resource = "Renamed.FloatColor", .m_Position = RGObservationPosition::Before, .m_Pass = "DeadConsumer" });
			context.Check(!missing.Resolve(graph)[0].m_Ready, "A default with only a culled consumer remains missing coverage");
			defaults.Register(NonFiniteWatchSemantic::TemporalInput, texture, consumer);
			context.Check(!defaults.Resolve(graph)[2].m_Ready, "A conflicting resource version cannot resolve through another valid binding");
			defaults.Register(NonFiniteWatchSemantic::AmbientOcclusion, original, consumer);
			context.Check(defaults.Resolve(graph)[1].m_Claim.m_Status == NonFiniteClaimStatus::Incomplete,
				"An inactive declaration with an active binding is inconsistent coverage");
			defaults.Declare({});
			context.Check(defaults.Resolve(graph)[0].m_Claim.m_Status == NonFiniteClaimStatus::Incomplete,
				"Duplicate pipeline declarations cannot overwrite applicability silently");
		}

		void RunNonFiniteGenerationDefaultTests(SelfTestContext& context) noexcept
		{
			CaptureTestDevice device;
			FrameCaptureService capacity(device, { .m_MaxPayloads = 4 });
			context.Check(capacity.GetNonFiniteScanTargetLimit() == 4 && capacity.EnableNonFiniteProduction() &&
				capacity.GetNonFiniteScanTargetLimit() == 2,
				"Generation scheduling reserves production readback and CPU payload slots in the same budget");
			TransientResourcePool pool(&device);
			RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
			auto& defaults = graph.GetBlackboard().GetOrCreate<NonFiniteWatchDefaultRegistry>();
			defaults.Declare({ false, false, false, false, true });
			RGTextureId cube;
			struct Data {};
			graph.AddPass<Data>("Import.Active", [&](auto& builder, auto&)
				{
					cube = builder.ImportTexture("Restored.Cube", RHITextureHandle{ 400, 1 },
						{ .m_Format = RHIFormat::R16G16B16A16Float, .m_Usage = RHITextureUsage::Sampled | RHITextureUsage::CopySource,
							.m_Extent = { 8, 8, 1 }, .m_ArraySize = 6, .m_MipLevels = 4 },
						CommonRHIResourceState(), RGContentValidity::Defined);
					defaults.RegisterGeneration(NonFiniteWatchSemantic::Environment, cube,
						builder.GetObservationAddress(cube, RGObservationPosition::Final), { { 400, 1 }, 9 });
					builder.Export(cube, RGTextureAccess::None);
					builder.SideEffect();
				});
			context.Check(graph.Compile(), "A cache-restored imported generation compiles without a producer pass");
			const auto resolved = defaults.Resolve(graph);
			const auto& environment = resolved[static_cast<size_t>(NonFiniteWatchSemantic::Environment)];
			context.Check(environment.m_Ready && environment.m_Generation.m_Revision == 9 && environment.m_Mips.size() == 4,
				"Generation defaults cover every imported mip without requiring a bake pass");
			context.Check(std::ranges::all_of(environment.m_Mips, [](const auto& address)
				{ return address.m_FirstSlice == 0 && address.m_SliceCount == 6u; }) && environment.m_Mips.back().m_Mip == 3,
				"Every cubemap mip scans all six faces including the smallest mip");
			const auto exported = graph.ResolveObservation(environment.m_Mips.back(), RGObservationKind::ResourceExport);
			context.Check(exported.m_Failure == RGObservationFailure::None && GetNonFiniteDefaultWatchTier(graph, exported) == 2u,
				"Export metadata identifies the exact registered generation version");
			defaults.RegisterGeneration(NonFiniteWatchSemantic::Environment, cube,
				{ .m_Resource = "Restored.Cube" }, { { 400, 1 }, 10 });
			context.Check(!defaults.Resolve(graph)[static_cast<size_t>(NonFiniteWatchSemantic::Environment)].m_Ready,
				"Conflicting generation identities cannot certify one resource version");
			NonFiniteWatchDefaultRegistry missing;
			missing.Declare({ false, false, false, false, true });
			missing.RegisterGeneration(NonFiniteWatchSemantic::Environment, cube, { .m_Resource = "Restored.Cube" }, {});
			context.Check(!missing.Resolve(graph)[static_cast<size_t>(NonFiniteWatchSemantic::Environment)].m_Ready,
				"Active generation bindings require a nonzero revision and physical resource identity");
		}

		void RunNonFiniteObserverTests(SelfTestContext& context) noexcept
		{
			CaptureTestDevice device;
			TransientResourcePool pool(&device);
			ScanTestPrograms programs;
			FrameCaptureService service(device, { .m_MaxPayloads = 4, .m_MaxPayloadBytes = 4096 });
			service.ConfigureNonFiniteScans(programs, programs, programs);
			struct Data { RGBufferId m_Buffer; };
			const auto build = [&](RenderGraph& graph)
				{
					RGBufferId buffer;
					graph.AddPass<Data>("Dead", RGPassEncoderType::Copy, [](auto& builder, auto&)
						{ GGLAB_UNUSED(builder.Write(builder.CreateBuffer("Dead.Buffer", { .m_SizeInBytes = 8 }), RGBufferAccess::CopyDest)); });
					graph.AddPass<Data>("Initialize", RGPassEncoderType::Copy, [&](auto& builder, auto& data)
						{
							buffer = builder.CreateBuffer("Scan.Buffer", { .m_SizeInBytes = 16, .m_StrideInBytes = 8 });
							data.m_Buffer = builder.Write(buffer, RGBufferAccess::CopyDest); buffer = data.m_Buffer;
						}, [&](auto& execution, auto& data)
						{
							const std::array<uint32_t, 4> words{ std::bit_cast<uint32_t>(1.0f), 0x7fc00000u, 0x7fc00000u, 0x7fc00000u };
							const auto handle = execution.GetBufferHandle(data.m_Buffer);
							std::memcpy(device.MapBuffer(handle, {}), words.data(), sizeof(words)); device.UnmapBuffer(handle, {});
						});
					graph.AddPass<Data>("Overwrite", RGPassEncoderType::Copy, [&](auto& builder, auto& data)
						{ data.m_Buffer = builder.ReadWrite(buffer, RGBufferAccess::StorageReadWrite); builder.SideEffect(); },
						[&](auto& execution, auto& data)
						{
							const auto handle = execution.GetBufferHandle(data.m_Buffer);
							*static_cast<uint32_t*>(device.MapBuffer(handle, {})) = 0xff800000u; device.UnmapBuffer(handle, {});
						});
					return graph.Compile();
				};
			const std::array fields{ GpuBufferFieldLayout{ "value", 0, GpuBufferScalar::Float32 }, GpuBufferFieldLayout{ "flags", 4, GpuBufferScalar::Uint32 } };
			const auto layout = BuildNonFiniteBufferScanLayout({ 8, fields }, 16, 1);
			const RGResourceAddress before{ .m_Resource = "Scan.Buffer", .m_Position = RGObservationPosition::Before, .m_Pass = "Overwrite" };
			const RGResourceAddress final{ .m_Resource = "Scan.Buffer" };
			RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
			context.Check(build(graph) && service.PrepareResourceExports(30, graph) == FrameCaptureAvailability::Available &&
				programs.m_LoadCount == 0 && programs.m_LayoutCount == 0 && !graph.HasNonFiniteScans(),
				"An unobserved frame neither demands scan programs nor adds scan nodes");
			const auto scanId = service.RequestNonFiniteScan({ { before, layout }, { final, layout } }, 19, 30, true);
			const auto copyId = service.RequestResourceExport({ before, final }, 30, false, true);
			context.Check(service.PrepareResourceExports(30, graph) == FrameCaptureAvailability::Available &&
				graph.HasNonFiniteScans() && graph.HasResourceObservations(), "Resource copies and boundary scans attach together to the same original graph");
			service.BeginGraphRecording(30, graph);
			RGSnapshot snapshot; BuildRenderGraphSnapshot(graph, snapshot);
			const auto json = nlohmann::json::parse(SerializeRenderGraphSnapshot(snapshot));
			context.Check(json["schemaVersion"] == 3 && json["compile"]["passes"][0]["culled"] == true &&
				json["compile"]["passes"][1]["executionOrder"] == 0 && json["compile"]["passes"][2]["executionOrder"] == 5,
				"Mixed observers preserve original culling and exact version-cut ordering");
			bool uav = false, copyTransition = false;
			for (const auto& pass : snapshot.m_Passes)
			{
				for (const auto& barrier : pass.m_PreBarriers)
				{
					uav |= barrier.m_Kind == RGBarrierKind::Uav;
					copyTransition |= Test(barrier.m_Before.m_Access, RHIAccess::UnorderedAccess) && Test(barrier.m_After.m_Access, RHIAccess::CopySource);
				}
			}
			context.Check(uav && copyTransition, "The general barrier planner supplies clear-to-scan UAV and scan-to-copy ordering");
			ExportTestCommands graphics(device); ScanTestCommands compute(device);
			RGExecuteContext execution({ .m_GraphicsCommandContext = &graphics, .m_DirectComputeCommandContext = &compute });
			graph.Execute(execution); service.EndGraphRecording(30, graph);
			context.Check(device.m_ReadbackMapCalls == 0 && compute.m_Dispatches == 4, "Recording scans does not map a readback or wait on the CPU");
			service.OnFrameSubmitted(30, MakeFence(10));
			context.Check(Consume(service).empty() && device.m_ReadbackMapCalls == 0, "Scan publication waits for its actual frame fence");
			device.m_CompletedFenceValue = 10;
			auto results = Consume(service);
			const auto scanned = std::ranges::find(results, scanId, &FrameCaptureResult::m_RequestId);
			const auto copied = std::ranges::find(results, copyId, &FrameCaptureResult::m_RequestId);
			context.Check(results.size() == 2 && scanned != results.end() && copied != results.end() && scanned->m_NonFiniteScans && copied->m_Resources,
				"Both transports publish exactly once after the common fence");
			if (scanned != results.end() && scanned->m_NonFiniteScans)
			{
				const auto& payload = *scanned->m_NonFiniteScans;
				context.Check(payload.m_ConfigurationVersion == 19 && scanned->m_FrameSerial == 30 && scanned->m_SubmissionFenceValue == 10 &&
					payload.m_Production.m_Status == NonFiniteClaimStatus::Unavailable, "Scan results preserve configuration/frame/fence identity and never claim production coverage");
				context.Check(payload.m_Boundaries.size() == 2 && payload.m_Boundaries[0].m_Result.m_Claim.m_Status == NonFiniteClaimStatus::Passed &&
					payload.m_Boundaries[1].m_Result.m_Claim.m_Failure == NonFiniteWatchFailure::NonFiniteDetected &&
					payload.m_Boundaries[1].m_Record.m_NegativeInfCount == 1 && payload.m_Boundaries[1].m_Record.m_CheckedCount == 1,
					"Observers see both exact versions and exclude integer flags and inactive capacity");
				context.Check(scanned->m_Graph && scanned->m_Graph->m_Execution.m_Recorded && scanned->m_NonFiniteScanActive &&
					copied != results.end() && copied->m_NonFiniteScanActive, "Mixed graph consumers report scan activity without fabricating watch coverage");
			}
			graph.Retire(MakeFence(10));
			RenderGraph next({ .m_Device = &device, .m_TransientResourcePool = &pool }); context.Check(build(next), "Retry fixture compiles");
			GGLAB_UNUSED(service.RequestNonFiniteScan({ { final, layout } }, 20, 31));
			GGLAB_UNUSED(service.PrepareResourceExports(31, next));
			auto busy = Consume(service);
			context.Check(busy.size() == 1 && busy[0].m_Status == FrameCaptureStatus::Busy && !next.HasNonFiniteScans(),
				"Retained CPU scan and resource payloads share the same bounded slots");
			results.clear(); busy.clear();
			const auto retry = service.RequestNonFiniteScan({ { final, layout } }, 21);
			GGLAB_UNUSED(service.PrepareResourceExports(32, next)); service.OnFrameAborted(32);
			context.Check(service.GetUnfinishedRequestCount() == 1, "An unsubmitted flexible scan returns to the queue");
			RenderGraph retryGraph({ .m_Device = &device, .m_TransientResourcePool = &pool }); GGLAB_UNUSED(build(retryGraph));
			GGLAB_UNUSED(service.PrepareResourceExports(33, retryGraph)); service.OnFrameSubmissionFailed(33);
			auto failed = Consume(service);
			context.Check(failed.size() == 1 && failed[0].m_RequestId == retry && failed[0].m_Status == FrameCaptureStatus::Failed &&
				service.GetUnfinishedRequestCount() == 0, "Submission failure releases a retried scan without publishing success");
			RenderGraph atomic({ .m_Device = &device, .m_TransientResourcePool = &pool }); GGLAB_UNUSED(build(atomic));
			const auto live = device.GetLiveBufferCount();
			GGLAB_UNUSED(service.RequestNonFiniteScan({ { final, layout }, { { .m_Resource = "missing" }, layout } }, 22, 34));
			GGLAB_UNUSED(service.PrepareResourceExports(34, atomic)); failed = Consume(service);
			context.Check(failed.size() == 1 && failed[0].m_ResourceFailure == RGObservationFailure::UnknownResource &&
				!atomic.HasNonFiniteScans() && device.GetLiveBufferCount() == live, "A missing target rejects its entire scan group before allocation or graph mutation");
			GGLAB_UNUSED(service.RequestNonFiniteScan({ { final, {} } }, 22, 35));
			GGLAB_UNUSED(service.PrepareResourceExports(35, atomic)); failed = Consume(service);
			context.Check(failed.size() == 1 && failed[0].m_Status == FrameCaptureStatus::Failed && !atomic.HasNonFiniteScans(),
				"A buffer without an explicit producer-valid interpretation cannot silently pass");
			FrameCaptureService tiny(device, { .m_MaxPayloadBytes = 95 }); tiny.ConfigureNonFiniteScans(programs, programs, programs);
			GGLAB_UNUSED(tiny.RequestNonFiniteScan({ { final, layout } }, 23, 36));
			GGLAB_UNUSED(tiny.PrepareResourceExports(36, atomic)); failed = Consume(tiny);
			context.Check(failed.size() == 1 && failed[0].m_ResourceFailure == RGObservationFailure::TooLarge && !atomic.HasNonFiniteScans(),
				"Scan admission includes record, readback, CPU and layout-upload bytes");
			programs.m_FailResolution = true;
			GGLAB_UNUSED(service.RequestNonFiniteScan({ { final, layout } }, 24, 37));
			GGLAB_UNUSED(service.PrepareResourceExports(37, atomic)); failed = Consume(service);
			context.Check(failed.size() == 1 && failed[0].m_Status == FrameCaptureStatus::Failed && !atomic.HasNonFiniteScans() && device.GetLiveBufferCount() == live,
				"Unavailable scan pipelines fail explicitly without attaching partial observers");
			programs.m_FailResolution = false;
			GGLAB_UNUSED(service.RequestNonFiniteScan({ { final, layout } }, 25, 38));
			GGLAB_UNUSED(service.PrepareResourceExports(38, atomic)); compute.m_SkipScan = true;
			atomic.Execute(execution); service.OnFrameSubmitted(38, MakeFence(11)); device.m_CompletedFenceValue = 11;
			failed = Consume(service); atomic.Retire(MakeFence(11));
			context.Check(failed.size() == 1 && failed[0].m_NonFiniteScans &&
				failed[0].m_NonFiniteScans->m_Boundaries[0].m_Result.m_Claim.m_Status == NonFiniteClaimStatus::Incomplete,
				"A completed transport with no checked scalars is Incomplete, not a boundary pass");
			failed.clear();
			RenderGraph mapFailure({ .m_Device = &device, .m_TransientResourcePool = &pool }); GGLAB_UNUSED(build(mapFailure));
			GGLAB_UNUSED(service.RequestNonFiniteScan({ { final, layout } }, 26, 39));
			GGLAB_UNUSED(service.PrepareResourceExports(39, mapFailure)); compute.m_SkipScan = false;
			mapFailure.Execute(execution); service.OnFrameSubmitted(39, MakeFence(12));
			device.m_CompletedFenceValue = 12; device.m_FailMapping = true;
			failed = Consume(service); mapFailure.Retire(MakeFence(12)); device.m_FailMapping = false;
			context.Check(failed.size() == 1 && failed[0].m_Status == FrameCaptureStatus::Failed && !failed[0].m_NonFiniteScans,
				"A readback mapping failure cannot publish a successful scan payload");
			RenderGraph imported({ .m_Device = &device, .m_TransientResourcePool = &pool });
			const RHIBufferDesc importDesc{ .m_SizeInBytes = 16, .m_StrideInBytes = 8, .m_Usage = RHIBufferUsage::Structured };
			RHIBufferOwner importBuffer(&device, device.CreateBuffer(importDesc, {}));
			imported.AddPass<Data>("Import", RGPassEncoderType::Compute, [&](auto& builder, auto&)
				{
					const auto input = builder.ImportBuffer("Imported.Scan", importBuffer.Get(), importDesc,
						RGBufferAccess::StructuredRead, RGContentValidity::Defined);
					GGLAB_UNUSED(builder.Read(input, RGBufferAccess::StructuredRead)); builder.SideEffect();
				});
			context.Check(imported.Compile() && imported.ResolveObservation({ .m_Resource = "Imported.Scan" }).m_Failure == RGObservationFailure::UnsupportedResource &&
				imported.ResolveObservation({ .m_Resource = "Imported.Scan" }, RGObservationKind::NonFiniteScan).m_Failure == RGObservationFailure::None,
				"Imported scan inputs require shader readability without weakening resource export's CopySource contract");
			const auto cancelled = service.RequestNonFiniteScan({ { { .m_Resource = "Imported.Scan" }, layout } }, 27, 40);
			GGLAB_UNUSED(service.PrepareResourceExports(40, imported)); service.OnFrameAborted(40); failed = Consume(service);
			context.Check(failed.size() == 1 && failed[0].m_RequestId == cancelled && failed[0].m_Status == FrameCaptureStatus::Failed &&
				service.GetUnfinishedRequestCount() == 0, "Aborting a frame-specific scan fails once and releases unsubmitted ownership");
			service.Shutdown(); tiny.Shutdown();
		}

		void RunResourceExportSupportTests(SelfTestContext& context) noexcept
		{
			RHITextureDesc preview{ .m_Format = RHIFormat::R8G8B8A8Unorm,
				.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled, .m_Extent = { 1024, 768, 1 } };
			context.Check(GetResourceExportSupport(preview, true) == RGObservationFailure::UnsupportedResource &&
				GetResourceExportSupport(preview, false) == RGObservationFailure::None,
				"Imported previews require creation-time CopySource while graph-owned textures can derive observation usage");
			RHITextureDesc cube = preview;
			cube.m_Format = RHIFormat::R16G16B16A16Float;
			cube.m_ArraySize = 6;
			cube.m_CreateFlags = RHITextureCreateFlags::CubeCompatible;
			cube.m_Usage |= RHITextureUsage::CopySource;
			context.Check(GetResourceExportSupport(cube, true) == RGObservationFailure::None,
				"A copy-capable imported HDR cubemap is discoverable for resource export");
			auto unsupported = cube;
			unsupported.m_Dimension = RHITextureDimension::Texture3D;
			context.Check(GetResourceExportSupport(unsupported, true) == RGObservationFailure::UnsupportedResource,
				"Three-dimensional textures are excluded by the Runtime export capability");
			unsupported = cube;
			unsupported.m_SampleCount = 4;
			context.Check(GetResourceExportSupport(unsupported, true) == RGObservationFailure::UnsupportedResource,
				"Multisampled textures are excluded by the Runtime export capability");
			unsupported = cube;
			unsupported.m_Format = RHIFormat::Unknown;
			context.Check(GetResourceExportSupport(unsupported, true) == RGObservationFailure::UnsupportedFormat,
				"Unknown formats do not become export candidates");
			unsupported = cube;
			unsupported.m_Extent.m_Width = 0;
			context.Check(GetResourceExportSupport(unsupported, true) == RGObservationFailure::UnsupportedResource,
				"Empty texture extents do not become export candidates");
			bool supportedFormats = true;
			for (uint32_t value = 1; value < static_cast<uint32_t>(RHIFormat::Count); ++value)
			{
				RHITextureDesc desc = cube;
				desc.m_Format = static_cast<RHIFormat>(value);
				supportedFormats &= GetResourceExportSupport(desc, true) == RGObservationFailure::None;
			}
			context.Check(supportedFormats, "Discovery keeps the current typed, typeless and depth format families supported by T2");
			RHIBufferDesc buffer{ .m_SizeInBytes = 32, .m_StrideInBytes = 8 };
			context.Check(GetResourceExportSupport(buffer, false) == RGObservationFailure::None &&
				GetResourceExportSupport(buffer, true) == RGObservationFailure::UnsupportedResource,
				"Buffer discovery distinguishes graph-owned usage from an imported allocation's copy contract");
			buffer.m_Usage = RHIBufferUsage::CopySource;
			context.Check(GetResourceExportSupport(buffer, true) == RGObservationFailure::None,
				"Copy-capable raw and structured buffers remain export candidates");
			buffer.m_StrideInBytes = 3;
			context.Check(GetResourceExportSupport(buffer, true) == RGObservationFailure::UnsupportedResource,
				"Unsupported buffer word layouts are excluded by the same rule used for array descriptions");

			CaptureTestDevice device;
			TransientResourcePool pool(&device);
			RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
			struct Data {};
			graph.AddPass<Data>("Discovery", [&](auto& builder, auto&)
				{
					auto previewTexture = builder.ImportTexture("Preview.IBL.EnvironmentCubemap", RHITextureHandle{ 10, 1 },
						preview, RGTextureAccess::Sample, RGContentValidity::Defined);
					auto environment = builder.ImportTexture("IBL.EnvironmentCubemap", RHITextureHandle{ 11, 1 },
						cube, RGTextureAccess::Sample, RGContentValidity::Defined);
					GGLAB_UNUSED(builder.Read(previewTexture, RGTextureAccess::Sample));
					GGLAB_UNUSED(builder.Read(environment, RGTextureAccess::Sample));
					auto words = builder.CreateBuffer("Words", { .m_SizeInBytes = 32 });
					builder.WriteInPlace(words, RGBufferAccess::CopyDest);
					builder.SideEffect();
				});
			context.Check(graph.Compile(), "Resource export discovery fixture compiles without inserting observers");
			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(graph, snapshot);
			const auto find = [&](std::string_view name)
				{ return std::ranges::find(snapshot.m_Resources, name, &RGSnapshotResourceInfo::m_Name); };
			const auto previewInfo = find("Preview.IBL.EnvironmentCubemap");
			const auto cubeInfo = find("IBL.EnvironmentCubemap");
			const auto wordsInfo = find("Words");
			context.Check(previewInfo != snapshot.m_Resources.end() && cubeInfo != snapshot.m_Resources.end() &&
				wordsInfo != snapshot.m_Resources.end() &&
				previewInfo->m_ResourceExportSupport == RGObservationFailure::UnsupportedResource &&
				cubeInfo->m_ResourceExportSupport == RGObservationFailure::None &&
				wordsInfo->m_ResourceExportSupport == RGObservationFailure::None && !graph.HasResourceObservations(),
				"Immutable diagnostics publish export capabilities without promoting imported usage or changing the graph");
			context.Check(graph.ResolveObservation({ .m_Resource = "Preview.IBL.EnvironmentCubemap" }).m_Failure ==
				RGObservationFailure::UnsupportedResource,
				"The imported preview hidden by discovery is rejected by actual address resolution for the same reason");
			const auto json = nlohmann::json::parse(SerializeRenderGraphSnapshot(snapshot));
			context.Check(json["schemaVersion"] == 1 && std::ranges::none_of(json["compile"]["resources"],
				[](const auto& resource) { return resource.contains("resourceExportSupport"); }),
				"UI export capability diagnostics leave the persisted graph schema unchanged");
		}

		void RunObservationStateTests(SelfTestContext& context) noexcept
		{
			CaptureTestDevice device; TransientResourcePool pool(&device); FrameCaptureService service(device);
			RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
			struct Data {};
			const RHITextureDesc desc{ .m_Format = RHIFormat::R8Unorm,
				.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::CopySource, .m_Extent = {2, 2, 1} };
			graph.AddPass<Data>("UseImported", RGPassEncoderType::Copy, [&](auto& builder, auto&)
				{
					auto texture = builder.ImportTexture("Imported", RHITextureHandle{10, 1}, desc,
						RGTextureAccess::Sample, RGContentValidity::Defined);
					GGLAB_UNUSED(builder.Read(texture, RGTextureAccess::Sample));
					builder.Export(texture, RGTextureAccess::Sample);
				});
			context.Check(graph.Compile(), "Imported export-state fixture compiles");
			GGLAB_UNUSED(service.RequestCapture(FrameCaptureSource::Scene, 11));
			const auto image = service.BindTap(11, FrameCaptureSource::Scene,
				{ .m_Format = RHIFormat::R8G8B8A8Unorm, .m_Usage = RHITextureUsage::CopySource, .m_Extent = {1, 1, 1} });
			GGLAB_UNUSED(service.RequestResourceExport({{ .m_Resource = "Imported" }}, 11, false, true));
			context.Check(image && service.PrepareResourceExports(11, graph) == FrameCaptureAvailability::Available,
				"A final observation can read a defined imported resource after its declared Export");
			RGSnapshot snapshot; BuildRenderGraphSnapshot(graph, snapshot);
			const auto json = nlohmann::json::parse(SerializeRenderGraphSnapshot(snapshot));
			context.Check(std::ranges::any_of(json["compile"]["passes"][1]["postBarriers"], [](const auto& barrier)
				{ return barrier["resource"] == "Imported" && barrier["reason"] == "FinalStateTransition" && barrier["before"]["layout"] == "CopySource"; }),
				"The last observer restores the imported owner's final state before returning ownership");
			service.BeginGraphRecording(11, graph); ExportTestCommands commands(device);
			RGExecuteContext execution({ .m_GraphicsCommandContext = &commands }); graph.Execute(execution); service.EndGraphRecording(11, graph);
			service.OnFrameSubmitted(11, MakeFence(5)); device.m_CompletedFenceValue = 5; service.CollectCompleted();
			std::vector<FrameCaptureResult> results; service.ConsumeResults(results);
			context.Check(results.size() == 2 && std::ranges::all_of(results, [](const auto& result)
				{ return result.m_Status == FrameCaptureStatus::Completed && result.m_GraphExportActive && result.m_ResourceExportActive; }),
				"Paired PNG and resource results both disclose every active observation tool");
			graph.Retire(MakeFence(5));

			RenderGraph views({ .m_Device = &device, .m_TransientResourcePool = &pool }); RGTextureId texture;
			views.AddPass<Data>("FloatWriter", [&](RenderGraph::RGBuilder& builder, Data&)
				{
					texture = builder.CreateTexture("Typeless", { .m_Format = RHIFormat::R32Typeless, .m_Extent = {2, 2, 1} });
					builder.WriteInPlace(texture, RGTextureAccess::StorageWrite);
					GGLAB_UNUSED(builder.CreateView<RHITextureViewType::UnorderedAccess>(texture, RHITextureViewDesc{ .m_Format = RHIFormat::R32Float }));
					builder.SideEffect();
				});
			views.AddPass<Data>("UintReader", [&](RenderGraph::RGBuilder& builder, Data&)
				{
					GGLAB_UNUSED(builder.Read(texture, RGTextureAccess::Sample));
					GGLAB_UNUSED(builder.CreateView<RHITextureViewType::ShaderResource>(texture, RHITextureViewDesc{ .m_Format = RHIFormat::R32Uint }));
					builder.SideEffect();
				});
			context.Check(views.Compile(), "Typeless views fixture compiles");
			const auto final = views.ResolveObservation({ .m_Resource = "Typeless" });
			const auto before = views.ResolveObservation({ .m_Resource = "Typeless", .m_Position = RGObservationPosition::Before, .m_Pass = "UintReader" });
			context.Check(final.m_ViewFormat == RHIFormat::R32Float && before.m_ViewFormat == RHIFormat::R32Uint && !final.m_RawTypeless && !before.m_RawTypeless,
				"Typeless interpretation follows the addressed pass instead of unrelated views elsewhere in the graph");
		}

		void RunResourceTextureExportTests(SelfTestContext& context) noexcept
		{
			CaptureTestDevice device;
			TransientResourcePool pool(&device);
			FrameCaptureService service(device);
			RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
			struct Data {};
			graph.AddPass<Data>("Initialize", RGPassEncoderType::Copy, [](auto& builder, auto&)
				{
					auto color = builder.CreateTexture("ColorArray", { .m_Format = RHIFormat::R8G8B8A8UnormSrgb,
						.m_Extent = {2, 2, 1}, .m_ArraySize = 2 });
					auto depth = builder.CreateTexture("Depth", { .m_Format = RHIFormat::D24UnormS8Uint, .m_Extent = {2, 2, 1} });
					builder.WriteInPlace(color, RGTextureAccess::CopyDest); builder.WriteInPlace(depth, RGTextureAccess::CopyDest);
					builder.SideEffect();
				});
			context.Check(graph.Compile(), "Texture export fixture compiles");
			GGLAB_UNUSED(service.RequestResourceExport({{ .m_Resource = "ColorArray" }, { .m_Resource = "Depth" },
				{ .m_Resource = "Depth", .m_Aspect = RHITextureAspect::Stencil }}, 10));
			context.Check(service.PrepareResourceExports(10, graph) == FrameCaptureAvailability::Available, "Color/depth/stencil group is admitted atomically");
			ExportTestCommands commands(device); RGExecuteContext execution({ .m_GraphicsCommandContext = &commands }); graph.Execute(execution);
			service.OnFrameSubmitted(10, MakeFence(4)); device.m_CompletedFenceValue = 4; service.CollectCompleted();
			std::vector<FrameCaptureResult> results; service.ConsumeResults(results);
			context.Check(results.size() == 1 && results[0].m_Resources && results[0].m_Resources->m_Arrays.size() == 3,
				"Texture groups publish all planes together");
			if (!results.empty() && results[0].m_Resources)
			{
				const auto& arrays = results[0].m_Resources->m_Arrays;
				float depth[2]; std::memcpy(depth, arrays[1].m_Data.data(), sizeof(depth));
				context.Check(arrays[0].m_Shape == std::vector<uint64_t>{2, 2, 2, 4} && arrays[0].m_Data.size() == 32 &&
					arrays[0].m_Data[8] == 8 && arrays[0].m_Data[16] == 32 && arrays[0].m_Encoding == "srgb",
					"Array slices/rows repack tightly without padding, swizzling or gamma conversion");
				context.Check(depth[0] == 0 && depth[1] == 1 && arrays[2].m_Scalar == ResourceExportScalar::Uint8 && arrays[2].m_Data.size() == 4,
					"D24 exports normalized depth and an explicitly selected byte stencil plane");
			}
			graph.Retire(MakeFence(4));
		}

		void RunResourceTextureSliceAlignmentTests(SelfTestContext& context) noexcept
		{
			struct TextureCase
			{
				uint32_t m_Height;
				uint16_t m_Slices;
				bool m_Cube;
			};
			constexpr std::array cases{ TextureCase{1, 2, false}, TextureCase{2, 2, false}, TextureCase{1, 6, true} };
			for (const auto& textureCase : cases)
			{
				CaptureTestDevice device;
				TransientResourcePool pool(&device);
				FrameCaptureService service(device);
				RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
				struct Data {};
				graph.AddPass<Data>("InitializeRGB", RGPassEncoderType::Copy, [&](auto& builder, auto&)
					{
						auto texture = builder.CreateTexture("RGB", { .m_Format = RHIFormat::R32G32B32Float,
							.m_CreateFlags = textureCase.m_Cube ? RHITextureCreateFlags::CubeCompatible : RHITextureCreateFlags::None,
							.m_Extent = {1, textureCase.m_Height, 1}, .m_ArraySize = textureCase.m_Slices });
						builder.WriteInPlace(texture, RGTextureAccess::CopyDest);
						builder.SideEffect();
					});
				context.Check(graph.Compile(), "RGB32F array/cube export fixture compiles");
				if (textureCase.m_Height == 1 && !textureCase.m_Cube)
				{
					const auto target = graph.ResolveObservation({ .m_Resource = "RGB" });
					const auto readback = device.CreateBuffer({ .m_SizeInBytes = 3072,
						.m_Usage = RHIBufferUsage::CopyDest, .m_MemoryUsage = RHIMemoryUsage::GpuToCpu }, {});
					const std::array invalidCopies{ RGObservationCopy{ .m_Target = target, .m_Readback = readback,
						.m_ReadbackBytes = 3072,
						.m_Footprint = ComputeRHIObservationCopyFootprint(target.m_TextureDesc, 0, RHITextureAspect::Color),
						.m_SlicePitch = 1024 } };
					context.Check(!graph.AttachObservationCopies(invalidCopies) && !graph.HasResourceObservations(),
						"RenderGraph rejects RGB32F slices aligned to 512 bytes but not to twelve-byte texels");
					device.DestroyBuffer(readback);
					FrameCaptureService bounded(device, { .m_MaxPayloadBytes = 3095 });
					const uint64_t rejected = bounded.RequestResourceExport({{ .m_Resource = "RGB" }}, 12);
					GGLAB_UNUSED(bounded.PrepareResourceExports(12, graph));
					const auto failures = Consume(bounded);
					context.Check(failures.size() == 1 && failures[0].m_RequestId == rejected &&
						failures[0].m_ResourceFailure == RGObservationFailure::TooLarge && !graph.HasResourceObservations(),
						"RGB32F capacity includes the aligned 3072-byte readback plus 24 tight CPU bytes");
				}
				const uint64_t id = service.RequestResourceExport({{ .m_Resource = "RGB" }}, 12);
				context.Check(service.PrepareResourceExports(12, graph) == FrameCaptureAvailability::Available && graph.HasResourceObservations(),
					"An RGB32F group binds with legal slice placement");
				ExportTestCommands commands(device);
				RGExecuteContext execution({ .m_GraphicsCommandContext = &commands });
				graph.Execute(execution);
				bool copiesAligned = commands.m_TextureCopies.size() == textureCase.m_Slices;
				for (size_t slice = 0; copiesAligned && slice < commands.m_TextureCopies.size(); ++slice)
				{
					const auto& copy = commands.m_TextureCopies[slice];
					copiesAligned = copy.m_SourceArraySlice == slice && copy.m_DestinationOffset == slice * 1536 &&
						copy.m_DestinationOffset % 512 == 0 && copy.m_DestinationOffset % 12 == 0 &&
						copy.m_Footprint.m_RowPitch == 768;
				}
				context.Check(copiesAligned, "Every RGB32F array/cube slice uses an offset divisible by both placement and texel size");
				service.OnFrameSubmitted(12, MakeFence(6));
				device.m_CompletedFenceValue = 6;
				service.CollectCompleted();
				const auto results = Consume(service);
				bool tightData = results.size() == 1 && results[0].m_RequestId == id &&
					results[0].m_Status == FrameCaptureStatus::Completed && results[0].m_Resources &&
					results[0].m_Resources->m_Arrays.size() == 1;
				if (tightData)
				{
					const auto& array = results[0].m_Resources->m_Arrays[0];
					tightData = array.m_Scalar == ResourceExportScalar::Float32 &&
						array.m_Shape == std::vector<uint64_t>{textureCase.m_Slices, textureCase.m_Height, 1, 3} &&
						array.m_Data.size() == static_cast<size_t>(textureCase.m_Slices) * textureCase.m_Height * 12;
					for (size_t byte = 0; tightData && byte < array.m_Data.size(); ++byte)
					{
						const size_t row = byte / 12;
						tightData = array.m_Data[byte] == static_cast<uint8_t>((row / textureCase.m_Height) * 32 + (row % textureCase.m_Height) * 8);
					}
				}
				context.Check(tightData, "RGB32F export preserves every slice and row while removing readback padding");
				graph.Retire(MakeFence(6));
			}
		}

		void RunScheduledResourceGroupTests(SelfTestContext& context) noexcept
		{
			CaptureTestDevice device;
			TransientResourcePool pool(&device);
			const RHITextureDesc imageDesc = MakeDisplayTarget(RHIFormat::R8G8B8A8Unorm, 1, 1);
			const std::vector<RGResourceAddress> eightTargets(8, RGResourceAddress{ .m_Resource = "Words" });
			const auto build = [](RenderGraph& graph)
				{
					struct Data {};
					graph.AddPass<Data>("Write", RGPassEncoderType::Copy, [](auto& builder, auto&)
						{
							auto words = builder.CreateBuffer("Words", { .m_SizeInBytes = 4 });
							builder.WriteInPlace(words, RGBufferAccess::CopyDest);
							builder.SideEffect();
						});
					return graph.Compile();
				};
			{
				FrameCaptureService service(device);
				RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
				context.Check(build(graph), "Scheduled export concurrency fixture compiles");
				const auto buffersBeforeTap = device.GetLiveBufferCount();
				const auto image = service.RequestCapture(FrameCaptureSource::Scene, 20);
				const auto diagnostic = service.RequestDiagnosticCapture(PostProcessDebugTap::GTAOFinalAO, 20);
				const auto resources = service.RequestResourceExport(eightTargets, 20, true, true, 101);
				context.Check(!service.HasPendingRequests(FrameCaptureSource::Diagnostic) && !service.GetPendingDiagnosticTap(),
					"An unrelated diagnostic defers before feature selection can fail it on the scheduled frame");
				context.Check(!service.BindTap(20, FrameCaptureSource::Scene, imageDesc) && device.GetLiveBufferCount() == buffersBeforeTap,
					"An unrelated image defers before allocating or attaching a copy to a scheduled export frame");
				context.Check(service.PrepareResourceExports(20, graph) == FrameCaptureAvailability::Available && graph.HasResourceObservations(),
					"An eight-target scheduled group is admitted despite an independent screenshot on its requested frame");
				const auto deferred = Consume(service);
				context.Check(deferred.size() == 2 && std::ranges::all_of(deferred, [&](const auto& result)
					{ return (result.m_RequestId == image || result.m_RequestId == diagnostic) && result.m_Status == FrameCaptureStatus::Busy && result.m_FrameSerial == 20; }),
					"Only the unrelated screenshots return Busy; the resource group is not mislabeled TooLarge");
				service.BeginGraphRecording(20, graph);
				ExportTestCommands commands(device);
				RGExecuteContext execution({ .m_GraphicsCommandContext = &commands });
				graph.Execute(execution);
				service.EndGraphRecording(20, graph);
				service.OnFrameSubmitted(20, MakeFence(20));
				device.m_CompletedFenceValue = 20;
				auto completed = Consume(service);
				context.Check(completed.size() == 1 && completed[0].m_RequestId == resources &&
					completed[0].m_Status == FrameCaptureStatus::Completed && completed[0].m_Resources && completed[0].m_Graph,
					"The scheduled group submits and publishes rather than rolling back every frame");
				graph.Retire(MakeFence(20));
				completed.clear();
				GGLAB_UNUSED(service.RequestCapture(FrameCaptureSource::Scene, 21));
				context.Check(service.BindTap(21, FrameCaptureSource::Scene, imageDesc).has_value(),
					"The deferred independent screenshot can bind on a later frame after its competing group completes");
				service.OnFrameAborted(21);
				GGLAB_UNUSED(Consume(service));
			}
			{
				FrameCaptureService service(device);
				RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
				GGLAB_UNUSED(build(graph));
				const auto unrelated = service.RequestCapture(FrameCaptureSource::Scene, 22);
				context.Check(service.BindTap(22, FrameCaptureSource::Scene, imageDesc).has_value(),
					"A prebound unrelated image occupies shared capacity before an export is requested");
				const auto resources = service.RequestResourceExport(eightTargets, 22, true, false, 102);
				context.Check(service.PrepareResourceExports(22, graph) == FrameCaptureAvailability::Busy && !graph.HasResourceObservations(),
					"Prebound unrelated image pressure is temporary Busy, not a permanent size error for the group");
				const auto deferred = Consume(service);
				context.Check(deferred.size() == 2 && std::ranges::all_of(deferred, [&](const auto& result)
					{ return (result.m_RequestId == unrelated || result.m_RequestId == resources) && result.m_Status == FrameCaptureStatus::Busy; }),
					"A genuinely busy frame withdraws its unsubmitted captures without publishing a TooLarge failure");
			}
			for (uint32_t targetCount : {7u, 8u})
			{
				FrameCaptureService service(device);
				RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
				GGLAB_UNUSED(build(graph));
				const auto image = service.RequestCapture(FrameCaptureSource::Scene, 23, 103);
				const auto shared = service.RequestCapture(FrameCaptureSource::Scene, 23);
				const auto unrelated = service.RequestCapture(FrameCaptureSource::Composited, 23);
				const auto resources = service.RequestResourceExport(std::vector<RGResourceAddress>(targetCount, eightTargets[0]), 23, true, false, 103);
				const auto diagnostic = service.RequestDiagnosticCapture(PostProcessDebugTap::GTAOFinalAO, 23);
				context.Check(!service.GetPendingDiagnosticTap(),
					"Diagnostics submitted after the group claim also defer before pipeline construction");
				const auto tap = service.BindTap(23, FrameCaptureSource::Scene, imageDesc);
				context.Check(tap && !service.BindTap(23, FrameCaptureSource::Composited, imageDesc),
					"A required group image may share its physical tap; a different independent image waits");
				GGLAB_UNUSED(service.PrepareResourceExports(23, graph));
				const auto pending = Consume(service);
				const auto other = std::ranges::find(pending, unrelated, &FrameCaptureResult::m_RequestId);
				const auto otherDiagnostic = std::ranges::find(pending, diagnostic, &FrameCaptureResult::m_RequestId);
				const auto exported = std::ranges::find(pending, resources, &FrameCaptureResult::m_RequestId);
				context.Check(other != pending.end() && other->m_Status == FrameCaptureStatus::Busy &&
					otherDiagnostic != pending.end() && otherDiagnostic->m_Status == FrameCaptureStatus::Busy &&
					(targetCount == 7 ? exported == pending.end() && graph.HasResourceObservations()
						: exported != pending.end() && exported->m_ResourceFailure == RGObservationFailure::TooLarge),
					"Only the group's required PNG counts in its permanent limit: seven targets fit and eight cannot");
				if (targetCount == 7)
				{
					ExportTestCommands commands(device);
					RGExecuteContext execution({ .m_GraphicsCommandContext = &commands });
					graph.Execute(execution);
					service.OnFrameSubmitted(23, MakeFence(23));
					device.FillReadback(tap->m_Buffer, tap->m_Footprint);
					device.m_CompletedFenceValue = 23;
					const auto completed = Consume(service);
					const auto imageResult = std::ranges::find(completed, image, &FrameCaptureResult::m_RequestId);
					const auto sharedResult = std::ranges::find(completed, shared, &FrameCaptureResult::m_RequestId);
					context.Check(completed.size() == 3 && std::ranges::all_of(completed, [](const auto& result) { return result.m_Status == FrameCaptureStatus::Completed; }) &&
						imageResult != completed.end() && sharedResult != completed.end() && imageResult->m_Image == sharedResult->m_Image,
						"The PNG group, its shared image consumer and the resource group complete on one submitted frame");
					graph.Retire(MakeFence(23));
				}
				else
				{
					service.OnFrameAborted(23);
					GGLAB_UNUSED(Consume(service));
				}
			}
			FrameCaptureService requests(device, { .m_MaxRequests = 2 });
			GGLAB_UNUSED(requests.RequestCapture(FrameCaptureSource::RenderGraph));
			context.Check(requests.GetRequestAvailability() == FrameCaptureAvailability::Available &&
				requests.GetRequestAvailability(2) == FrameCaptureAvailability::Busy &&
				requests.GetRequestAvailability(3) == FrameCaptureAvailability::TooLarge,
				"Request admission distinguishes a temporarily missing group slot from an impossible request count");
			requests.Shutdown();
		}

		void RunGraphRequestCapacityTests(SelfTestContext& context) noexcept
		{
			CaptureTestDevice device;
			TransientResourcePool pool(&device);
			FrameCaptureService service(device, { .m_MaxRequests = 2 });
			RenderGraph graph({ .m_Device = &device, .m_TransientResourcePool = &pool });
			context.Check(graph.Compile(), "Graph capacity fixture compiles");
			GGLAB_UNUSED(service.RequestCapture(FrameCaptureSource::RenderGraph, 1));
			GGLAB_UNUSED(service.RequestCapture(FrameCaptureSource::RenderGraph, 1));
			context.Check(service.GetRequestAvailability() == FrameCaptureAvailability::Busy,
				"Graph admission uses the configured request limit rather than 64");
			service.BeginGraphRecording(1, graph);
			RGExecuteContext execute({});
			graph.Execute(execute);
			service.EndGraphRecording(1, graph);
			context.Check(!graph.TakeExecutionRecord(), "Ending graph recording transfers the execution record exactly once");
			service.OnFrameSubmitted(1, MakeFence(1));
			device.m_CompletedFenceValue = 1;
			service.CollectCompleted();
			context.Check(service.GetUnfinishedRequestCount() == 0 &&
				service.GetRequestAvailability() == FrameCaptureAvailability::Busy &&
				service.RequestCapture(FrameCaptureSource::RenderGraph) == 0,
				"Unconsumed graph results continue to occupy request admission slots");
			context.Check(Consume(service).size() == 2 &&
				service.GetRequestAvailability() == FrameCaptureAvailability::Available,
				"Consuming graph results restores request-only capacity");
			service.Shutdown();
			context.Check(service.GetRequestAvailability() == FrameCaptureAvailability::Finalized,
				"Request-only availability distinguishes a finalized service");
		}

		void RunFixedTimeStepTests(SelfTestContext& context) noexcept
		{
			Time time;
			time.Initialize();
			time.SetFixedDeltaTime(0.25);
			time.Update();
			time.Update();
			context.Check(time.GetDeltaTime() == 0.25 && time.GetTotalTime() == 0.5 &&
				time.GetFixedDeltaTime() == 0.25 && time.GetFrameCount() == 2,
				"A fixed time step advances delta and total time independent of wall clock");
			time.SetFixedDeltaTime(std::nullopt);
			time.Update();
			context.Check(!time.GetFixedDeltaTime() && time.GetDeltaTime() != 0.25,
				"Clearing the fixed step returns to wall-clock time");
		}
	}

	void RunFrameCaptureSelfTests(SelfTestContext& context) noexcept
	{
		RunNonFiniteDefaultTests(context);
		RunNonFiniteGenerationDefaultTests(context);
		RunNonFiniteObserverTests(context);
		RunResourceExportTests(context);
		RunResourceTextureExportTests(context);
		RunResourceTextureSliceAlignmentTests(context);
		RunScheduledResourceGroupTests(context);
		RunResourceExportSupportTests(context);
		RunObservationStateTests(context);
		RunGraphCaptureTests(context);
		RunGraphEnumTests(context);
		RunGraphVersionTests(context);
		RunGraphRequestCapacityTests(context);
		RunFixedTimeStepTests(context);
		RunFootprintTests(context);
		RunConversionTests(context);
		RunCompletionTests(context);
		RunDiagnosticTests(context);
		RunFrameBindingTests(context);
		RunFrameEndTests(context);
		RunCapacityTests(context);
	}
}
