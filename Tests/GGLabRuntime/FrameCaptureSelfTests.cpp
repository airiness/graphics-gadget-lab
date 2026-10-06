#include "FrameCaptureSelfTests.h"

#include "GGLabRuntime/Core/Time.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "Graphics/Capture/FrameCaptureService.h"

#include <algorithm>
#include <array>
#include <cstdint>
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
				return {};
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
			bool IsAlive(RHITextureHandle) const noexcept override { return false; }
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
			int32_t m_MapCount = 0;
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
		RunFixedTimeStepTests(context);
		RunFootprintTests(context);
		RunConversionTests(context);
		RunCompletionTests(context);
		RunFrameEndTests(context);
	}
}
