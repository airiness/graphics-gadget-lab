#include "FrameCaptureSelfTests.h"

#include "GGLabRuntime/Core/Time.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
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
				return {};
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

			[[nodiscard]] const RHIBufferDesc* FindBufferDesc(RHIBufferHandle buffer) const noexcept
			{
				const auto entry = m_Buffers.find(buffer);
				return entry != m_Buffers.end() ? &entry->second.m_Desc : nullptr;
			}
			[[nodiscard]] size_t GetLiveBufferCount() const noexcept { return m_Buffers.size(); }
			[[nodiscard]] int32_t GetMapCount() const noexcept { return m_MapCount; }

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
				auto* dest = static_cast<uint8_t*>(m_Device.MapBuffer(destination, {}));
				const auto* src = static_cast<const uint8_t*>(m_Device.MapBuffer(source, {}));
				std::memcpy(dest + destinationOffset, src + sourceOffset, static_cast<size_t>(bytes));
				m_Device.UnmapBuffer(source, {});
				m_Device.UnmapBuffer(destination, {});
			}
			void CopyTextureToBuffer(const RHITextureToBufferCopy& copy) noexcept override
			{
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
		private:
			CaptureTestDevice& m_Device;
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
			if (sceneTap) device.FillReadback(sceneTap->m_Buffer, sceneTap->m_Footprint);
			if (diagnosticTap) device.FillReadback(diagnosticTap->m_Buffer, diagnosticTap->m_Footprint);
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
			if (retryTap) device.FillReadback(retryTap->m_Buffer, retryTap->m_Footprint);
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
				if (!ids.empty()) ids += ',';
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
					else context.Check(access["writtenVersion"].is_null() && access["writtenResourceNodeIndex"].is_null(),
						"Read-only accesses have no written version");
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
		RunResourceExportTests(context);
		RunResourceTextureExportTests(context);
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
