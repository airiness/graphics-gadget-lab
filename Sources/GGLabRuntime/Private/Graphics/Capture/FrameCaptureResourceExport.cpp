#include "Graphics/Capture/FrameCaptureService.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGExecutionPlan.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlus.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>
#include <limits>

namespace gglab
{
	std::string_view GetResourceExportDtype(ResourceExportScalar scalar) noexcept
	{
		switch (scalar)
		{
		case ResourceExportScalar::Uint8: return "|u1";
		case ResourceExportScalar::Uint32: return "<u4";
		case ResourceExportScalar::Float16: return "<f2";
		case ResourceExportScalar::Float32: return "<f4";
		}
		return "Unknown";
	}

	namespace
	{
		std::optional<ResourceExportSemantics> Semantics(std::string_view name, ResourceExportFrameContext context)
		{
			if (name == "DisplayView.SceneColor" || name == "Atmosphere.AerialSceneColor" ||
				name == "TAA.ResolvedSceneColor" || name == "TAA.CurrentSceneColor")
			{
				return ResourceExportSemantics{ .m_Role = name == "TAA.ResolvedSceneColor" ? "scene-color.post-temporal" : "scene-color.pre-temporal",
					.m_Units = "scene-linear Rec.709, pre-exposed", .m_Space = "display view",
					.m_Description = "Stored radiance is scene-linear radiance multiplied by preExposure.",
					.m_PreExposure = context.m_PreExposure, .m_WatchTier = 1 };
			}
			if (name == "DisplayView.DepthBuffer" || name == "DisplayView.DisplayDepth")
			{
				return ResourceExportSemantics{ .m_Role = "depth", .m_Units = "device depth [0, 1]", .m_Space = "display view",
					.m_Description = "Projection depth; reversedDepth records the selected view convention.",
					.m_ReversedDepth = context.m_ReversedDepth, .m_WatchTier = 1 };
			}
			if (name == "Temporal.MotionVectors")
			{
				return ResourceExportSemantics{ .m_Role = "motion-vectors", .m_Units = "UV displacement", .m_Space = "display view",
					.m_Description = "Current UV minus previous UV; positive X points right and positive Y points down. Previous UV = current UV - motion.", .m_WatchTier = 1 };
			}
			if (name == "GTAO.FinalAO" || name == "GTAO.TemporalAO" || name == "GTAO.RawAO" ||
				name == "GTAO.DenoiseX" || name == "GTAO.DenoiseY")
			{
				return ResourceExportSemantics{ .m_Role = "ambient-occlusion", .m_Units = "dimensionless [0, 1]",
					.m_Space = "display view", .m_Description = "Ambient visibility; one is unoccluded.", .m_WatchTier = 2 };
			}
			return std::nullopt;
		}

		bool DescribeTextureFormat(RHIFormat format, RHITextureAspect aspect, bool rawTypeless,
			ResourceExportScalar& scalar, uint32_t& channels) noexcept
		{
			channels = 1;
			if (aspect == RHITextureAspect::Stencil)
			{
				scalar = ResourceExportScalar::Uint8;
				return true;
			}
			if (rawTypeless)
			{
				const auto bytes = GetRHIFormatInfo(format).m_BytesPerBlock;
				if (bytes == 0 || bytes % 4 != 0)
				{
					return false;
				}
				channels = bytes / 4;
				scalar = ResourceExportScalar::Uint32;
				return true;
			}
			switch (format)
			{
			case RHIFormat::R8Unorm: scalar = ResourceExportScalar::Uint8; break;
			case RHIFormat::R8G8B8A8Unorm:
			case RHIFormat::R8G8B8A8UnormSrgb:
			case RHIFormat::B8G8R8A8Unorm:
			case RHIFormat::B8G8R8A8UnormSrgb: scalar = ResourceExportScalar::Uint8; channels = 4; break;
			case RHIFormat::R16Float: scalar = ResourceExportScalar::Float16; break;
			case RHIFormat::R16G16Float: scalar = ResourceExportScalar::Float16; channels = 2; break;
			case RHIFormat::R16G16B16A16Float: scalar = ResourceExportScalar::Float16; channels = 4; break;
			case RHIFormat::R32Float:
			case RHIFormat::D32Float:
			case RHIFormat::D24UnormS8Uint: scalar = ResourceExportScalar::Float32; break;
			case RHIFormat::R32G32Float: scalar = ResourceExportScalar::Float32; channels = 2; break;
			case RHIFormat::R32G32B32Float: scalar = ResourceExportScalar::Float32; channels = 3; break;
			case RHIFormat::R32G32B32A32Float: scalar = ResourceExportScalar::Float32; channels = 4; break;
			case RHIFormat::R32Uint: scalar = ResourceExportScalar::Uint32; break;
			default: return false;
			}
			return true;
		}

		bool DescribeArray(const RGObservationTarget& target, ResourceExportArray& array,
			ResourceExportFrameContext context) noexcept
		{
			array.m_Address = target.m_Address;
			array.m_Type = target.m_Type;
			array.m_ResourceNodeIndex = target.m_Node.Value();
			array.m_Version = target.m_Resource->GetVersion();
			array.m_WriterPassIndex = target.m_Writer.IsValid() ? static_cast<int32_t>(target.m_Writer.Value()) : -1;
			array.m_WriterPass = target.m_WriterName;
			array.m_WriterOccurrence = target.m_WriterOccurrence;
			array.m_OriginalLastUse = target.m_OriginalLastUse;
			array.m_Semantics = Semantics(target.m_Address.m_Resource, context);
			if (target.m_Type == RGResourceType::RGBuffer)
			{
				const auto& desc = target.m_BufferDesc;
				const uint32_t stride = desc.m_StrideInBytes == 0 ? 4 : desc.m_StrideInBytes;
				if (GetResourceExportSupport(desc, false) != RGObservationFailure::None)
				{
					return false;
				}
				array.m_Stride = stride;
				array.m_ElementCount = desc.m_SizeInBytes / stride;
				array.m_Shape = { array.m_ElementCount, stride / 4 };
				GpuBufferLayout layout;
				if (target.m_Address.m_Resource == "ForwardPlus.TileLightHeaders")
				{
					layout = ForwardPlusTileHeaderLayout;
				}
				if (target.m_Address.m_Resource == "ForwardPlus.TileDepthRanges")
				{
					layout = ForwardPlusTileDepthRangeLayout;
				}
				if (layout.m_Stride == stride && !layout.m_Fields.empty())
				{
					for (const auto& field : layout.m_Fields)
						array.m_Fields.push_back({ field.m_Name, field.m_Offset, field.m_Scalar, field.m_Count });
					array.m_Shape = { array.m_ElementCount };
				}
				array.m_Encoding = "raw";
				return true;
			}
			array.m_PhysicalFormat = target.m_TextureDesc.m_Format;
			array.m_ViewFormat = target.m_ViewFormat;
			array.m_RawTypeless = target.m_RawTypeless;
			const auto aspect = target.m_Subresources.m_Aspects;
			uint32_t channels = 1;
			const auto format = target.m_ViewFormat;
			if (!DescribeTextureFormat(target.m_RawTypeless ? target.m_TextureDesc.m_Format : format,
				aspect, target.m_RawTypeless, array.m_Scalar, channels))
			{
				return false;
			}
			array.m_Address.m_Mip = target.m_Subresources.m_BaseMip;
			array.m_Address.m_FirstSlice = target.m_Subresources.m_BaseArraySlice;
			array.m_Address.m_SliceCount = target.m_Subresources.m_ArraySliceCount;
			array.m_Address.m_Aspect = aspect;
			array.m_Shape = { target.m_Subresources.m_ArraySliceCount,
				std::max(1u, target.m_TextureDesc.m_Extent.m_Height >> target.m_Subresources.m_BaseMip),
				std::max(1u, target.m_TextureDesc.m_Extent.m_Width >> target.m_Subresources.m_BaseMip), channels };
			array.m_ChannelOrder = channels == 4 ? ((format == RHIFormat::B8G8R8A8Unorm || format == RHIFormat::B8G8R8A8UnormSrgb) ? "BGRA" : "RGBA")
				: channels == 3 ? "RGB" : channels == 2 ? "RG" : "R";
			array.m_Encoding = format == RHIFormat::D24UnormS8Uint && aspect == RHITextureAspect::Depth ? "normalized-depth"
				: (format == RHIFormat::R8G8B8A8UnormSrgb || format == RHIFormat::B8G8R8A8UnormSrgb) ? "srgb"
				: array.m_Scalar == ResourceExportScalar::Uint8 && aspect == RHITextureAspect::Color ? "unorm" : "raw";
			return true;
		}
	}

	RGObservationFailure GetResourceExportSupport(const RHITextureDesc& desc, bool imported) noexcept
	{
		if (desc.m_Dimension != RHITextureDimension::Texture2D || desc.m_SampleCount != 1 ||
			desc.m_Extent.m_Width == 0 || desc.m_Extent.m_Height == 0 ||
			(imported && !Test(desc.m_Usage, RHITextureUsage::CopySource)))
		{
			return RGObservationFailure::UnsupportedResource;
		}
		const auto& info = GetRHIFormatInfo(desc.m_Format);
		if (info.m_BlockWidth != 1 || info.m_BlockHeight != 1 || info.m_BytesPerBlock == 0)
		{
			return RGObservationFailure::UnsupportedFormat;
		}
		const auto aspects = GetRHITextureAspects(desc);
		const auto aspect = Test(aspects, RHITextureAspect::Depth) ? RHITextureAspect::Depth : RHITextureAspect::Color;
		ResourceExportScalar scalar;
		uint32_t channels;
		return DescribeTextureFormat(desc.m_Format, aspect, info.m_IsTypeless, scalar, channels)
			? RGObservationFailure::None : RGObservationFailure::UnsupportedFormat;
	}

	RGObservationFailure GetResourceExportSupport(const RHIBufferDesc& desc, bool imported) noexcept
	{
		const uint32_t stride = desc.m_StrideInBytes == 0 ? 4 : desc.m_StrideInBytes;
		if (desc.m_SizeInBytes == 0 || stride % 4 != 0 || desc.m_SizeInBytes % stride != 0 ||
			(imported && !Test(desc.m_Usage, RHIBufferUsage::CopySource)))
		{
			return RGObservationFailure::UnsupportedResource;
		}
		return RGObservationFailure::None;
	}

	std::pair<uint64_t, uint32_t> FrameCaptureService::GetPayloadUsage() const noexcept
	{
		uint64_t bytes = 0;
		uint32_t slots = 0;
		for (const auto& tap : m_Taps)
		{
			if (tap.m_Source == FrameCaptureSource::RenderGraph)
			{
				continue;
			}
			if (tap.m_Source == FrameCaptureSource::ResourceExport)
			{
				for (const auto& readback : tap.m_ExportReadbacks)
				{
					bytes += readback.m_Copy.m_ReadbackBytes + readback.m_CpuBytes; ++slots;
				}
			}
			else { bytes += tap.m_Footprint.m_SizeInBytes + tap.m_Footprint.m_RowSizeInBytes * tap.m_Footprint.m_Height; ++slots; }
		}
		for (const auto& image : m_RetainedImages)
			if (!image.m_Image.expired()) { bytes += image.m_Bytes; ++slots; }
		for (const auto& payload : m_RetainedExports)
			if (!payload.m_Payload.expired()) { bytes += payload.m_Bytes; slots += payload.m_Slots; }
		return { bytes, slots };
	}

	uint64_t FrameCaptureService::RequestResourceExport(std::vector<RGResourceAddress> targets,
		uint64_t frameSerial, bool deferFrameOnBusy, bool includeGraph) noexcept
	{
		if (GetRequestAvailability() == FrameCaptureAvailability::Busy)
		{
			return 0;
		}
		const uint64_t id = m_NextRequestId++;
		if (m_IsShutdown || targets.empty() || targets.size() > m_Limits.m_MaxPayloads)
		{
			m_Results.push_back({ .m_RequestId = id, .m_Source = FrameCaptureSource::ResourceExport,
				.m_ResourceFailure = targets.size() > m_Limits.m_MaxPayloads ? RGObservationFailure::TooLarge : RGObservationFailure::UnsupportedResource,
				.m_Failure = m_IsShutdown ? "The render host has been finalized." : "The resource export group is empty or exceeds the payload slot limit." });
			return id;
		}
		m_Queued.push_back({ .m_Id = id, .m_Source = FrameCaptureSource::ResourceExport, .m_FrameSerial = frameSerial,
			.m_ResourceTargets = std::move(targets), .m_DeferFrameOnBusy = deferFrameOnBusy, .m_IncludeGraph = includeGraph });
		return id;
	}

	FrameCaptureAvailability FrameCaptureService::GetResourceExportFrameAvailability() const noexcept
	{
		if (m_IsShutdown)
		{
			return FrameCaptureAvailability::Finalized;
		}
		if (GetRequestAvailability() != FrameCaptureAvailability::Available || GetPayloadUsage().second != 0)
		{
			return FrameCaptureAvailability::Busy;
		}
		return FrameCaptureAvailability::Available;
	}

	void FrameCaptureService::DeferFrameCaptures(uint64_t frameSerial) noexcept
	{
		const auto defer = [&](const QueuedRequest& request)
			{
				if (request.m_FrameSerial == 0) { m_Queued.push_back(request); return; }
				m_Results.push_back({ .m_RequestId = request.m_Id, .m_Source = request.m_Source,
					.m_Status = FrameCaptureStatus::Busy, .m_FrameSerial = frameSerial,
					.m_ResourceFailure = RGObservationFailure::Busy });
			};
		std::erase_if(m_Taps, [&](const Tap& tap)
			{
				if (tap.m_FrameSerial != frameSerial || tap.m_Submitted)
				{
					return false;
				}
				for (const auto& request : tap.m_Requests) defer(request);
				return true;
			});
		std::erase_if(m_Queued, [&](const QueuedRequest& request)
			{
				if (request.m_FrameSerial != frameSerial)
				{
					return false;
				}
				m_Results.push_back({ .m_RequestId = request.m_Id, .m_Source = request.m_Source,
					.m_Status = FrameCaptureStatus::Busy, .m_FrameSerial = frameSerial,
					.m_ResourceFailure = RGObservationFailure::Busy });
				return true;
			});
		std::ranges::sort(m_Queued, {}, &QueuedRequest::m_Id);
	}

	FrameCaptureAvailability FrameCaptureService::PrepareResourceExports(uint64_t frameSerial,
		RenderGraph& graph, ResourceExportFrameContext context) noexcept
	{
		if (m_IsShutdown)
		{
			return FrameCaptureAvailability::Finalized;
		}
		if (!HasPendingRequests(FrameCaptureSource::ResourceExport))
		{
			return FrameCaptureAvailability::Available;
		}
		std::erase_if(m_RetainedExports, [](const auto& entry) { return entry.m_Payload.expired(); });
		std::vector<Tap> prepared;
		auto [usedBytes, usedSlots] = GetPayloadUsage();
		uint64_t pairedBytes = 0;
		uint32_t pairedSlots = 0;
		for (const auto& tap : m_Taps)
			if (tap.m_FrameSerial == frameSerial && tap.m_Source != FrameCaptureSource::RenderGraph &&
				tap.m_Source != FrameCaptureSource::ResourceExport)
			{
				pairedBytes += tap.m_Footprint.m_SizeInBytes + tap.m_Footprint.m_RowSizeInBytes * tap.m_Footprint.m_Height; ++pairedSlots;
			}
		bool deferFrame = false;
		// Exact sequence groups claim this frame before flexible concurrent exports.
		std::stable_sort(m_Queued.begin(), m_Queued.end(), [](const auto& a, const auto& b)
			{ return a.m_DeferFrameOnBusy && !b.m_DeferFrameOnBusy; });
		std::erase_if(m_Queued, [&](const QueuedRequest& request)
			{
				if (request.m_Source != FrameCaptureSource::ResourceExport ||
					(request.m_FrameSerial != 0 && request.m_FrameSerial != frameSerial))
				{
					return false;
				}
				Tap tap{ .m_FrameSerial = frameSerial, .m_Source = FrameCaptureSource::ResourceExport };
				tap.m_Requests.push_back(request);
				uint64_t bytes = 0;
				RGObservationFailure failure = RGObservationFailure::None;
				for (const auto& address : request.m_ResourceTargets)
				{
					ExportReadback readback;
					readback.m_Copy.m_Target = graph.ResolveObservation(address);
					failure = readback.m_Copy.m_Target.m_Failure;
					if (failure != RGObservationFailure::None)
					{
						break;
					}
					const auto& target = readback.m_Copy.m_Target;
					if (!DescribeArray(target, readback.m_Array, context)) { failure = RGObservationFailure::UnsupportedFormat; break; }
					if (target.m_Type == RGResourceType::RGTexture)
					{
						readback.m_Copy.m_Footprint = ComputeRHIObservationCopyFootprint(target.m_TextureDesc,
							target.m_Subresources.m_BaseMip, target.m_Subresources.m_Aspects);
						const auto& footprint = readback.m_Copy.m_Footprint;
						if (!footprint.IsValid()) { failure = RGObservationFailure::UnsupportedFormat; break; }
						if (footprint.m_SizeInBytes > m_Limits.m_MaxPayloadBytes) { failure = RGObservationFailure::TooLarge; break; }
						readback.m_Copy.m_SlicePitch = (footprint.m_SizeInBytes + RHITextureCopyPlacementAlignment - 1) /
							RHITextureCopyPlacementAlignment * RHITextureCopyPlacementAlignment;
						if (readback.m_Copy.m_SlicePitch > m_Limits.m_MaxPayloadBytes / target.m_Subresources.m_ArraySliceCount)
						{
							failure = RGObservationFailure::TooLarge; break;
						}
						readback.m_Copy.m_ReadbackBytes = readback.m_Copy.m_SlicePitch * target.m_Subresources.m_ArraySliceCount;
						readback.m_CpuBytes = footprint.m_RowSizeInBytes * footprint.m_Height * target.m_Subresources.m_ArraySliceCount;
					}
					else
					{
						readback.m_Copy.m_ReadbackBytes = readback.m_CpuBytes = target.m_BufferDesc.m_SizeInBytes;
					}
					if (readback.m_Copy.m_ReadbackBytes > m_Limits.m_MaxPayloadBytes ||
						readback.m_CpuBytes > m_Limits.m_MaxPayloadBytes - readback.m_Copy.m_ReadbackBytes ||
						bytes > m_Limits.m_MaxPayloadBytes - readback.m_Copy.m_ReadbackBytes - readback.m_CpuBytes)
					{
						failure = RGObservationFailure::TooLarge; break;
					}
					bytes += readback.m_Copy.m_ReadbackBytes + readback.m_CpuBytes;
					tap.m_ExportReadbacks.push_back(std::move(readback));
				}
				if (failure == RGObservationFailure::None && request.m_DeferFrameOnBusy &&
					(pairedSlots + tap.m_ExportReadbacks.size() > m_Limits.m_MaxPayloads || bytes > m_Limits.m_MaxPayloadBytes - pairedBytes))
				{
					failure = RGObservationFailure::TooLarge;
				}
				if (failure == RGObservationFailure::None && (usedSlots + tap.m_ExportReadbacks.size() > m_Limits.m_MaxPayloads ||
					bytes > m_Limits.m_MaxPayloadBytes - usedBytes))
				{
					failure = RGObservationFailure::Busy;
				}
				if (failure != RGObservationFailure::None)
				{
					if (failure == RGObservationFailure::Busy && request.m_DeferFrameOnBusy)
					{
						deferFrame = true; return false;
					}
					m_Results.push_back({ .m_RequestId = request.m_Id, .m_Source = request.m_Source,
						.m_Status = failure == RGObservationFailure::Busy ? FrameCaptureStatus::Busy : FrameCaptureStatus::Failed,
						.m_FrameSerial = frameSerial, .m_ResourceFailure = failure,
						.m_Failure = std::format("{}: resource export group could not bind the selected frame.", GetRGObservationFailureName(failure)) });
					return true;
				}
				usedBytes += bytes;
				usedSlots += static_cast<uint32_t>(tap.m_ExportReadbacks.size());
				tap.m_AdapterCompatibilityIdentity = m_Device->GetAdapterCompatibilityIdentity();
				prepared.push_back(std::move(tap));
				return true;
			});
		if (deferFrame)
		{
			for (auto& tap : prepared) for (auto& request : tap.m_Requests) m_Queued.push_back(std::move(request));
			DeferFrameCaptures(frameSerial);
			return FrameCaptureAvailability::Busy;
		}
		std::vector<RGObservationCopy> copies;
		for (auto& tap : prepared)
		{
			bool allocated = true;
			for (auto& readback : tap.m_ExportReadbacks)
			{
				readback.m_Buffer = RHIBufferOwner(m_Device, m_Device->CreateBuffer({ .m_SizeInBytes = readback.m_Copy.m_ReadbackBytes,
					.m_Usage = RHIBufferUsage::CopyDest, .m_MemoryUsage = RHIMemoryUsage::GpuToCpu, .m_DebugName = "ResourceExport.Readback" }));
				if (!readback.m_Buffer) { allocated = false; break; }
				readback.m_Copy.m_Readback = readback.m_Buffer.Get();
			}
			if (!allocated) { FailTap(tap, frameSerial, "Resource export readback allocation failed."); tap.m_ExportReadbacks.clear(); continue; }
			for (const auto& readback : tap.m_ExportReadbacks) copies.push_back(readback.m_Copy);
		}
		if (!graph.AttachObservationCopies(copies))
		{
			for (auto& tap : prepared)
			{
				if (!tap.m_Requests.empty())
				{
					FailTap(tap, frameSerial, "Resource export graph extension failed.");
				}
			}
			return FrameCaptureAvailability::Available;
		}
		if (!copies.empty())
		{
			for (auto& consumer : m_Taps)
			{
				if (consumer.m_FrameSerial == frameSerial)
				{
					consumer.m_ResourceExportActive = true;
				}
			}
		}
		for (auto& tap : prepared)
		{
			if (tap.m_Requests.empty())
			{
				continue;
			}
			for (auto& readback : tap.m_ExportReadbacks)
				readback.m_Array.m_ObservedLastUse = graph.GetExecutionPlan()->GetResources()[
					readback.m_Copy.m_Target.m_VirtualResource.Value()].m_LastUser.Value();
					m_Taps.push_back(std::move(tap));
		}
		return FrameCaptureAvailability::Available;
	}

	void FrameCaptureService::PublishResourceTap(Tap& tap) noexcept
	{
		if (tap.m_Graph && !tap.m_Graph->m_Execution.m_Recorded)
		{
			FailTap(tap, tap.m_FrameSerial, "The resource export's paired graph did not execute.");
			return;
		}
		static_assert(std::endian::native == std::endian::little);
		auto payload = std::make_shared<ResourceExportPayload>();
		uint64_t retainedBytes = 0;
		for (auto& readback : tap.m_ExportReadbacks)
		{
			const auto* mapped = static_cast<const uint8_t*>(m_Device->MapBuffer(readback.m_Buffer.Get(),
				{ .m_Begin = 0, .m_End = readback.m_Copy.m_ReadbackBytes }));
			if (!mapped) { FailTap(tap, tap.m_FrameSerial, "Resource export readback mapping failed."); return; }
			auto& array = readback.m_Array;
			array.m_Data.resize(static_cast<size_t>(readback.m_CpuBytes));
			if (array.m_Type == RGResourceType::RGBuffer)
			{
				std::memcpy(array.m_Data.data(), mapped, array.m_Data.size());
			}
			else
			{
				const auto& footprint = readback.m_Copy.m_Footprint;
				for (uint32_t slice = 0; slice < array.m_Shape[0]; ++slice)
					for (uint32_t row = 0; row < footprint.m_Height; ++row)
						std::memcpy(array.m_Data.data() + (slice * footprint.m_Height + row) * footprint.m_RowSizeInBytes,
							mapped + slice * readback.m_Copy.m_SlicePitch + row * footprint.m_RowPitch, footprint.m_RowSizeInBytes);
				if (array.m_PhysicalFormat == RHIFormat::D24UnormS8Uint && array.m_Address.m_Aspect == RHITextureAspect::Depth)
				{
					for (size_t offset = 0; offset < array.m_Data.size(); offset += 4)
					{
						uint32_t packed; std::memcpy(&packed, array.m_Data.data() + offset, 4);
						const float depth = static_cast<float>(packed & 0x00ffffffu) / 16777215.0f;
						std::memcpy(array.m_Data.data() + offset, &depth, 4);
					}
				}
			}
			m_Device->UnmapBuffer(readback.m_Buffer.Get(), {});
			retainedBytes += array.m_Data.size();
			payload->m_Arrays.push_back(std::move(array));
		}
		m_RetainedExports.push_back({ payload, retainedBytes, static_cast<uint32_t>(payload->m_Arrays.size()) });
		for (const auto& request : tap.m_Requests)
			m_Results.push_back({ .m_RequestId = request.m_Id, .m_Source = FrameCaptureSource::ResourceExport,
				.m_Status = FrameCaptureStatus::Completed, .m_FrameSerial = tap.m_FrameSerial, .m_Graph = tap.m_Graph, .m_Resources = payload,
				.m_GraphExportActive = tap.m_GraphExportActive, .m_ResourceExportActive = true,
				.m_SubmissionFenceIndex = tap.m_Fence.m_Fence.Index(),
				.m_SubmissionFenceGeneration = tap.m_Fence.m_Fence.Generation(), .m_SubmissionFenceValue = tap.m_Fence.m_Value,
				.m_AdapterCompatibilityIdentity = tap.m_AdapterCompatibilityIdentity });
		tap.m_Requests.clear();
	}
}
