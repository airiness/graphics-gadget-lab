#include "Graphics/Capture/CaptureProducers.h"
#include "Graphics/Capture/CaptureProduct.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"

#include <bit>
#include <cstring>
#include <utility>

namespace gglab
{
	GraphCaptureProducer::GraphCaptureProducer(RenderGraph& graph) noexcept :
		m_Snapshot(std::make_shared<RGSnapshot>())
	{
		BuildRenderGraphSnapshot(graph, *m_Snapshot);
		graph.EnableExecutionRecord();
	}

	void GraphCaptureProducer::Finish(RGSnapshotExecutionRecord record) noexcept
	{
		GGLAB_ASSERT_MSG(!m_Frozen, "A published graph product is immutable.");
		if (!m_Frozen)
		{
			m_Snapshot->m_Execution = std::move(record);
			m_Frozen = true;
		}
	}

	bool GraphCaptureProducer::IsRecorded() const noexcept
	{
		return m_Frozen && m_Snapshot->m_Execution.m_Recorded;
	}

	std::shared_ptr<const RGSnapshot> GraphCaptureProducer::GetProduct() const noexcept
	{
		return IsRecorded() ? m_Snapshot : nullptr;
	}

	std::pair<uint64_t, uint32_t> ResourceCaptureProducer::GetPayloadCost() const noexcept
	{
		uint64_t bytes = 0;
		for (const auto& item : m_Readbacks)
		{
			bytes += item.m_Copy.m_ReadbackBytes + item.m_CpuBytes;
		}
		return { bytes, static_cast<uint32_t>(m_Readbacks.size()) };
	}

	bool ResourceCaptureProducer::Allocate(RHIDevice& device, FencedReadback& readback) noexcept
	{
		for (auto& item : m_Readbacks)
		{
			item.m_Copy.m_Readback = readback.AddBuffer(RHIBufferOwner(&device, device.CreateBuffer({
				.m_SizeInBytes = item.m_Copy.m_ReadbackBytes, .m_Usage = RHIBufferUsage::CopyDest,
				.m_MemoryUsage = RHIMemoryUsage::GpuToCpu, .m_DebugName = "ResourceExport.Readback" })));
			if (!item.m_Copy.m_Readback.IsValid())
			{
				return false;
			}
		}
		return true;
	}

	std::shared_ptr<const ResourceExportPayload> ResourceCaptureProducer::ReadProduct(
		RHIDevice& device, FencedReadback& readback, CapturePayloadBudget::Lease& lease) noexcept
	{
		if (!readback.IsCompleted())
		{
			return nullptr;
		}
		static_assert(std::endian::native == std::endian::little);
		ResourceExportPayload payload;
		uint64_t retainedBytes = 0;
		for (auto& item : m_Readbacks)
		{
			auto& array = item.m_Array;
			const bool copied = readback.Read(device, item.m_Copy.m_Readback,
				{ 0, item.m_Copy.m_ReadbackBytes }, [&](std::span<const std::byte> mapped)
				{
					array.m_Data.resize(static_cast<size_t>(item.m_CpuBytes));
					if (array.m_Type == RGResourceType::RGBuffer)
					{
						std::memcpy(array.m_Data.data(), mapped.data(), array.m_Data.size());
					}
					else
					{
						const auto& footprint = item.m_Copy.m_Footprint;
						for (uint32_t slice = 0; slice < array.m_Shape[0]; ++slice)
						{
							for (uint32_t row = 0; row < footprint.m_Height; ++row)
							{
								std::memcpy(array.m_Data.data() +
									(slice * footprint.m_Height + row) * footprint.m_RowSizeInBytes,
									mapped.data() + slice * item.m_Copy.m_SlicePitch +
									row * footprint.m_RowPitch, footprint.m_RowSizeInBytes);
							}
						}
						if (array.m_PhysicalFormat == RHIFormat::D24UnormS8Uint &&
							array.m_Address.m_Aspect == RHITextureAspect::Depth)
						{
							for (size_t offset = 0; offset < array.m_Data.size(); offset += 4)
							{
								uint32_t packed;
								std::memcpy(&packed, array.m_Data.data() + offset, 4);
								const float depth = static_cast<float>(packed & 0x00ffffffu) / 16777215.0f;
								std::memcpy(array.m_Data.data() + offset, &depth, 4);
							}
						}
					}
				});
			if (!copied)
			{
				return nullptr;
			}
			retainedBytes += array.m_Data.size();
			payload.m_Arrays.push_back(std::move(array));
		}
		readback.ReleaseBuffers();
		lease.ReduceTo(retainedBytes);
		return MakeCaptureProduct(std::move(payload), std::move(lease));
	}

	bool ScanCaptureReadback::Allocate(RHIDevice& device, FencedReadback& readback) noexcept
	{
		m_Scan.m_Readback = readback.AddBuffer(RHIBufferOwner(&device, device.CreateBuffer({
			.m_SizeInBytes = sizeof(NonFiniteScanRecord), .m_Usage = RHIBufferUsage::CopyDest,
			.m_MemoryUsage = RHIMemoryUsage::GpuToCpu, .m_DebugName = "NonFiniteScan.Readback" })));
		if (!m_Scan.m_Readback.IsValid())
		{
			return false;
		}
		if (m_Boundary.m_Target.m_Type == RGResourceType::RGBuffer)
		{
			m_Scan.m_Components = readback.AddBuffer(RHIBufferOwner(&device, device.CreateBuffer({
				.m_SizeInBytes = m_Scan.m_ComponentBytes,
				.m_StrideInBytes = sizeof(NonFiniteBufferComponent), .m_Usage = RHIBufferUsage::Structured,
				.m_MemoryUsage = RHIMemoryUsage::CpuToGpu, .m_DebugName = "NonFiniteScan.Components" })));
			if (!m_Scan.m_Components.IsValid())
			{
				return false;
			}
			void* mapped = device.MapBuffer(m_Scan.m_Components, {});
			if (!mapped)
			{
				return false;
			}
			std::memcpy(mapped, m_Boundary.m_BufferLayout.m_Components.data(),
				static_cast<size_t>(m_Scan.m_ComponentBytes));
			device.UnmapBuffer(m_Scan.m_Components, { 0, m_Scan.m_ComponentBytes });
		}
		return true;
	}

	std::pair<uint64_t, uint32_t> ScanCaptureProducer::GetPayloadCost() const noexcept
	{
		uint64_t bytes = 0;
		for (const auto& item : m_Readbacks)
		{
			bytes += item.GetPayloadBytes();
		}
		return { bytes, m_Readbacks.empty() ? 0u : 1u };
	}

	std::shared_ptr<const NonFiniteScanPayload> ScanCaptureProducer::ReadProduct(
		RHIDevice& device, FencedReadback& readback, CapturePayloadBudget::Lease& lease,
		uint64_t configurationVersion) noexcept
	{
		if (!readback.IsCompleted())
		{
			return nullptr;
		}
		NonFiniteScanPayload payload;
		payload.m_ConfigurationVersion = configurationVersion;
		uint64_t retainedBytes = 0;
		for (auto& item : m_Readbacks)
		{
			if (!readback.Read(device, item.m_Scan.m_Readback, { 0, sizeof(NonFiniteScanRecord) },
				[&](std::span<const std::byte> bytes)
				{
					std::memcpy(&item.m_Boundary.m_Record, bytes.data(), sizeof(NonFiniteScanRecord));
				}))
			{
				return nullptr;
			}
			item.m_Boundary.m_Result = EvaluateNonFiniteBoundary(item.m_Boundary.m_Record,
				item.m_Boundary.m_Parameters.m_ScalarCount);
			retainedBytes += sizeof(NonFiniteScanRecord) +
				item.m_Boundary.m_BufferLayout.m_Components.size() * sizeof(NonFiniteBufferComponent);
			payload.m_Boundaries.push_back(std::move(item.m_Boundary));
		}
		readback.ReleaseBuffers();
		lease.ReduceTo(retainedBytes);
		return MakeCaptureProduct(std::move(payload), std::move(lease));
	}

	bool ProductionCaptureProducer::Allocate(RHIDevice& device, FencedReadback& readback) noexcept
	{
		readback.SetBuffer(RHIBufferOwner(&device, device.CreateBuffer({
			.m_SizeInBytes = nonfinite::BufferBytes, .m_Usage = RHIBufferUsage::CopyDest,
			.m_MemoryUsage = RHIMemoryUsage::GpuToCpu, .m_DebugName = "NonFiniteCounters.Readback" })));
		return readback.GetBuffer().IsValid();
	}

	std::shared_ptr<const NonFiniteProductionFrame> ProductionCaptureProducer::ReadProduct(
		RHIDevice& device, FencedReadback& readback, CapturePayloadBudget::Lease& lease,
		uint64_t frameSerial) noexcept
	{
		if (!readback.IsCompleted())
		{
			return nullptr;
		}
		NonFiniteProductionFrame frame;
		frame.m_FrameSerial = frameSerial;
		const bool copied = readback.Read(device, readback.GetBuffer(), { 0, nonfinite::BufferBytes },
			[&](std::span<const std::byte> bytes)
			{
				frame = DecodeNonFiniteProductionFrame(bytes, frameSerial);
			});
		if (!copied)
		{
			frame.m_Failure = "Production counter readback mapping failed.";
		}
		else
		{
			frame.m_SubmissionFenceIndex = readback.GetFence().m_Fence.Index();
			frame.m_SubmissionFenceGeneration = readback.GetFence().m_Fence.Generation();
			frame.m_SubmissionFenceValue = readback.GetFence().m_Value;
			frame.m_AdapterCompatibilityIdentity = device.GetAdapterCompatibilityIdentity();
		}
		readback.ReleaseBuffers();
		lease.ReduceTo(CpuBytes);
		return MakeCaptureProduct(std::move(frame), std::move(lease));
	}
}
