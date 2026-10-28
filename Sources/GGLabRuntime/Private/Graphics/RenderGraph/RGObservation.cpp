#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGCompiler.h"
#include "Graphics/RenderGraph/RGExecutionPlan.h"
#include "Graphics/RHI/RHISubresourceUtils.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"

#include <algorithm>
#include <format>
#include <limits>

namespace gglab
{
	RGResourceAddress RenderGraph::RGBuilder::GetObservationAddress(
		RGResourceHandle resource, RGObservationPosition position) const noexcept
	{
		if (!resource.IsValid() || resource.GetHandle().Value() >= m_RG.m_ResourceSlots.size())
		{
			return {};
		}
		const auto* virtualResource = m_RG.GetVirtualResource(resource);
		if (!virtualResource)
		{
			return {};
		}
		const auto& pass = m_RG.m_PassNodes[m_PassNodeIndex.Value()];
		return { .m_Resource = std::string(virtualResource->m_NameId.Name()), .m_Position = position,
			.m_Pass = std::string(pass.m_NameId.Name()), .m_Occurrence = pass.m_NameOccurrence };
	}

	std::string_view GetRGObservationFailureName(RGObservationFailure failure) noexcept
	{
		switch (failure)
		{
		case RGObservationFailure::None:
			return "None";
		case RGObservationFailure::UnknownResource:
			return "UnknownResource";
		case RGObservationFailure::UnknownPass:
			return "UnknownPass";
		case RGObservationFailure::AmbiguousPass:
			return "AmbiguousPass";
		case RGObservationFailure::NotAccessedByPass:
			return "NotAccessedByPass";
		case RGObservationFailure::Culled:
			return "Culled";
		case RGObservationFailure::UndefinedContent:
			return "UndefinedContent";
		case RGObservationFailure::UnsupportedResource:
			return "UnsupportedResource";
		case RGObservationFailure::UnsupportedFormat:
			return "UnsupportedFormat";
		case RGObservationFailure::TooLarge:
			return "TooLarge";
		case RGObservationFailure::Busy:
			return "Busy";
		}
		return "Unknown";
	}

	RGObservationTarget RenderGraph::ResolveObservation(const RGResourceAddress& address, RGObservationKind kind) const noexcept
	{
		RGObservationTarget result{ .m_Address = address, .m_Kind = kind };
		const auto fail = [&](RGObservationFailure failure)
			{
				result.m_Failure = failure;
				return result;
			};
		if (!m_ExecutionPlan || !m_ObservationPasses.empty())
		{
			return fail(RGObservationFailure::UndefinedContent);
		}
		const auto resource = m_NameHandleMap.find(StringID{ Crc64(address.m_Resource) });
		if (resource == m_NameHandleMap.end())
		{
			return fail(RGObservationFailure::UnknownResource);
		}
		const auto& slot = m_ResourceSlots[resource->second.GetHandle().Value()];
		result.m_VirtualResource = slot.m_VirtualResourceIndex;
		const auto& compiled = m_ExecutionPlan->GetResources()[result.m_VirtualResource.Value()];
		if (compiled.m_NameId.Name() != address.m_Resource)
		{
			return fail(RGObservationFailure::UnknownResource);
		}
		result.m_Type = compiled.m_ResourceType;
		if (result.m_Type == RGResourceType::RGAccelerationStructure)
		{
			return fail(RGObservationFailure::UnsupportedResource);
		}
		result.m_Node = slot.m_ResourceNodeIndex;
		RGPassNodeIndex addressedPass;
		result.m_InsertionPosition = static_cast<uint32_t>(m_ExecutionPlan->GetExecutionOrder().size());
		result.m_OriginalLastUse = compiled.m_LastUser.IsValid() ?
			compiled.m_LastUser.Value() :
			std::numeric_limits<uint32_t>::max();
		if (address.m_Position != RGObservationPosition::Final)
		{
			RGPassNodeIndex selected;
			uint32_t count = 0;
			for (uint32_t i = 0; i < m_PassNodes.size(); ++i)
			{
				if (m_PassNodes[i].m_NameId.Name() != address.m_Pass)
				{
					continue;
				}
				++count;
				if (!address.m_Occurrence || m_PassNodes[i].m_NameOccurrence == *address.m_Occurrence)
				{
					selected = RGPassNodeIndex{ i };
				}
			}
			if (!selected.IsValid())
			{
				return fail(RGObservationFailure::UnknownPass);
			}
			if (!address.m_Occurrence && count != 1)
			{
				return fail(RGObservationFailure::AmbiguousPass);
			}
			const auto& pass = m_ExecutionPlan->GetPasses()[selected.Value()];
			if (pass.m_Culled)
			{
				return fail(RGObservationFailure::Culled);
			}
			addressedPass = selected;
			result.m_InsertionPosition = static_cast<uint32_t>(pass.m_ExecutionOrder) +
				(address.m_Position == RGObservationPosition::After ? 1u : 0u);
			RGResourceNodeIndex node;
			for (const auto& access : m_PassNodes[selected.Value()].m_Accesses)
			{
				if (m_ResourceNodes[access.m_ResourceNodeIndex.Value()].m_VirtualResource != compiled.m_Resource)
				{
					continue;
				}
				const bool write = address.m_Position == RGObservationPosition::After;
				if (write ?
					access.m_DependencyAccess == RGDependencyAccess::Read :
					access.m_DependencyAccess == RGDependencyAccess::Write)
				{
					continue;
				}
				const auto candidate = write ? access.m_WrittenResourceNodeIndex : access.m_ResourceNodeIndex;
				if (!node.IsValid() || candidate.Value() > node.Value())
				{
					node = candidate;
				}
			}
			if (!node.IsValid())
			{
				return fail(RGObservationFailure::NotAccessedByPass);
			}
			result.m_Node = node;
		}
		const auto& node = m_ResourceNodes[result.m_Node.Value()];
		result.m_Resource = node.m_ResourceHandle;
		result.m_Writer = node.m_Writer;
		if (!addressedPass.IsValid())
		{
			addressedPass = node.m_Writer;
		}
		if (node.m_Writer.IsValid())
		{
			if (m_ExecutionPlan->GetPasses()[node.m_Writer.Value()].m_Culled)
			{
				return fail(RGObservationFailure::Culled);
			}
			result.m_WriterName = m_PassNodes[node.m_Writer.Value()].m_NameId.Name();
			result.m_WriterOccurrence = m_PassNodes[node.m_Writer.Value()].m_NameOccurrence;
		}
		if (result.m_Type == RGResourceType::RGBuffer)
		{
			result.m_BufferDesc = static_cast<const RGVirtualResource<RGBufferResource>*>(compiled.m_Resource)->m_Desc;
			result.m_BufferDesc.m_DebugName = nullptr;
			if (address.m_Mip != 0 || address.m_FirstSlice != 0 || address.m_SliceCount ||
				address.m_Aspect != RHITextureAspect::None || (compiled.m_Imported &&
					!Test(result.m_BufferDesc.m_Usage, kind == RGObservationKind::ResourceExport ?
						RHIBufferUsage::CopySource :
						RHIBufferUsage::Structured)))
			{
				return fail(RGObservationFailure::UnsupportedResource);
			}
			if (node.m_ContentValidity != RGContentValidity::Defined)
			{
				return fail(RGObservationFailure::UndefinedContent);
			}
		}
		else
		{
			result.m_TextureDesc = static_cast<const RGVirtualResource<RGTextureResource>*>(compiled.m_Resource)->m_Desc;
			result.m_TextureDesc.m_DebugName = nullptr;
			if (!compiled.m_Imported)
			{
				result.m_TextureDesc.m_Usage = static_cast<RHITextureUsage>(compiled.m_UsageBits);
				if (kind == RGObservationKind::NonFiniteScan)
				{
					result.m_TextureDesc.m_Usage |= RHITextureUsage::Sampled;
				}
			}
			const auto& desc = result.m_TextureDesc;
			if (desc.m_Dimension != RHITextureDimension::Texture2D || desc.m_SampleCount != 1 ||
				address.m_Mip >= GetRHITextureMipLevelCount(desc) || address.m_Mip >= 32 ||
				address.m_FirstSlice >= GetRHITextureArraySize(desc) ||
				(compiled.m_Imported && !Test(desc.m_Usage, kind == RGObservationKind::ResourceExport ?
					RHITextureUsage::CopySource :
					RHITextureUsage::Sampled)))
			{
				return fail(RGObservationFailure::UnsupportedResource);
			}
			const uint32_t count = address.m_SliceCount.value_or(GetRHITextureArraySize(desc) - address.m_FirstSlice);
			const auto aspects = GetRHITextureAspects(desc);
			const auto aspect = address.m_Aspect == RHITextureAspect::None ?
				(Test(aspects, RHITextureAspect::Depth) ? RHITextureAspect::Depth : RHITextureAspect::Color) :
				address.m_Aspect;
			if (count == 0 || count > GetRHITextureArraySize(desc) - address.m_FirstSlice ||
				(aspect != RHITextureAspect::Color && aspect != RHITextureAspect::Depth && aspect != RHITextureAspect::Stencil) ||
				!Test(aspects, aspect))
			{
				return fail(RGObservationFailure::UnsupportedResource);
			}
			result.m_Subresources = { address.m_Mip, 1, address.m_FirstSlice, count, aspect };
			if (!IsTextureSubresourceRangeDefined(node.m_TextureValidity, desc, result.m_Subresources))
			{
				return fail(RGObservationFailure::UndefinedContent);
			}
			result.m_ViewFormat = desc.m_Format;
			if (GetRHIFormatInfo(desc.m_Format).m_IsTypeless)
			{
				result.m_ViewFormat = RHIFormat::Unknown;
				bool ambiguous = false;
				for (uint32_t i = 0; i < m_TextureViews.size(); ++i)
				{
					if (m_TextureViews[i].m_Pass != addressedPass)
					{
						continue;
					}
					const auto& view = m_ExecutionPlan->GetTextureViews()[i];
					if (view.m_Resource != result.m_VirtualResource || view.m_Desc.m_Format == RHIFormat::Unknown)
					{
						continue;
					}
					const auto range = NormalizeTextureSubresourceRange(desc, view.m_Desc.m_Subresources);
					if (address.m_Mip < range.m_BaseMip || address.m_Mip - range.m_BaseMip >= range.m_MipCount ||
						address.m_FirstSlice >= range.m_BaseArraySlice + range.m_ArraySliceCount ||
						address.m_FirstSlice + count <= range.m_BaseArraySlice || !Test(range.m_Aspects, aspect))
					{
						continue;
					}
					if (result.m_ViewFormat != RHIFormat::Unknown && result.m_ViewFormat != view.m_Desc.m_Format)
					{
						ambiguous = true;
					}
					result.m_ViewFormat = view.m_Desc.m_Format;
				}
				result.m_RawTypeless = ambiguous || result.m_ViewFormat == RHIFormat::Unknown;
				if (result.m_RawTypeless)
				{
					result.m_ViewFormat = RHIFormat::Unknown;
				}
			}
			if (aspect == RHITextureAspect::Depth && GetRHIFormatInfo(desc.m_Format).m_Family == RHIFormatFamily::R32)
			{
				result.m_ViewFormat = RHIFormat::D32Float;
				result.m_RawTypeless = false;
			}
		}

		// A version is addressable only while all selected contents still match it.
		// Partial writes inherit producers from earlier nodes; no culled producer is revived.
		const auto producer = [&](RGResourceNodeIndex version, uint32_t mip, uint32_t slice, RHITextureAspect aspect)
			{
				for (auto current = version; current.IsValid(); current = m_ResourceNodes[current.Value()].m_Previous)
				{
					const auto writer = m_ResourceNodes[current.Value()].m_Writer;
					if (!writer.IsValid())
					{
						continue;
					}
					for (const auto& access : m_PassNodes[writer.Value()].m_Accesses)
					{
						if (access.m_WrittenResourceNodeIndex != current)
						{
							continue;
						}
						if (result.m_Type == RGResourceType::RGBuffer)
						{
							return current;
						}
						const auto range = NormalizeTextureSubresourceRange(result.m_TextureDesc, access.m_Subresources);
						if (mip >= range.m_BaseMip && mip - range.m_BaseMip < range.m_MipCount &&
							slice >= range.m_BaseArraySlice && slice - range.m_BaseArraySlice < range.m_ArraySliceCount &&
							Test(range.m_Aspects, aspect))
						{
							return current;
						}
					}
				}
				return RGResourceNodeIndex{};
			};
		const auto checkContents = [&](uint32_t mip, uint32_t slice, RHITextureAspect aspect)
			{
				const auto expected = producer(result.m_Node, mip, slice, aspect);
				if (expected.IsValid() && m_ExecutionPlan->GetPasses()[m_ResourceNodes[expected.Value()].m_Writer.Value()].m_Culled)
				{
					return RGObservationFailure::Culled;
				}
				RGResourceNodeIndex actual;
				for (uint32_t i = 0; i < result.m_InsertionPosition; ++i)
				{
					for (const auto& access : m_PassNodes[m_ExecutionPlan->GetExecutionOrder()[i].Value()].m_Accesses)
					{
						if (!access.m_WrittenResourceNodeIndex.IsValid() ||
							m_ResourceNodes[access.m_ResourceNodeIndex.Value()].m_VirtualResource != compiled.m_Resource)
						{
							continue;
						}
						if (result.m_Type == RGResourceType::RGTexture)
						{
							const auto range = NormalizeTextureSubresourceRange(result.m_TextureDesc, access.m_Subresources);
							if (mip < range.m_BaseMip || mip - range.m_BaseMip >= range.m_MipCount ||
								slice < range.m_BaseArraySlice || slice - range.m_BaseArraySlice >= range.m_ArraySliceCount ||
								!Test(range.m_Aspects, aspect))
							{
								continue;
							}
						}
						actual = access.m_WrittenResourceNodeIndex;
					}
				}
				return actual == expected ? RGObservationFailure::None : RGObservationFailure::UndefinedContent;
			};
		if (result.m_Type == RGResourceType::RGBuffer)
		{
			result.m_Failure = checkContents(0, 0, RHITextureAspect::None);
		}
		else
		{
			ForEachRHITextureSubresource(result.m_TextureDesc, result.m_Subresources,
				[&](uint32_t mip, uint32_t slice, RHITextureAspect aspect)
				{
					if (result.m_Failure == RGObservationFailure::None)
					{
						result.m_Failure = checkContents(mip, slice, aspect);
					}
				});
		}
		return result;
	}

	bool RenderGraph::HasResourceObservations() const noexcept
	{
		return std::ranges::any_of(m_ObservationPasses, [](const auto& entry)
			{
				return entry.second.m_Kind == RGObservationKind::ResourceExport;
			});
	}

	bool RenderGraph::HasNonFiniteScans() const noexcept
	{
		return std::ranges::any_of(m_ObservationPasses, [](const auto& entry)
			{
				return entry.second.m_Kind != RGObservationKind::ResourceExport;
			});
	}

	bool RenderGraph::AttachObservationCopies(std::span<const RGObservationCopy> copies) noexcept
	{
		return AttachObservations(copies, {});
	}

	bool RenderGraph::AttachObservations(std::span<const RGObservationCopy> copies,
		std::span<const RGObservationScan> scans, const RGProductionCounters* counters) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutionPlan && m_ObservationPasses.empty(), "Observations extend one original compiled graph exactly once.");
		if (!m_ExecutionPlan || !m_ObservationPasses.empty())
		{
			return false;
		}
		if (copies.empty() && scans.empty() && !counters)
		{
			return true;
		}
		std::vector<RGObservationCopy> resolved(copies.begin(), copies.end());
		for (auto& copy : resolved)
		{
			const auto target = ResolveObservation(copy.m_Target.m_Address);
			if (copy.m_Target.m_Failure != RGObservationFailure::None || !copy.m_Readback.IsValid() ||
				!m_Device->IsAlive(copy.m_Readback) || copy.m_ReadbackBytes == 0 ||
				target.m_Failure != RGObservationFailure::None || target.m_Node != copy.m_Target.m_Node ||
				target.m_Resource != copy.m_Target.m_Resource)
			{
				return false;
			}
			copy.m_Target = target;
			if (target.m_Type == RGResourceType::RGBuffer)
			{
				if (copy.m_ReadbackBytes < target.m_BufferDesc.m_SizeInBytes)
				{
					return false;
				}
			}
			else
			{
				const auto expected = ComputeRHIObservationCopyFootprint(target.m_TextureDesc,
					target.m_Subresources.m_BaseMip, target.m_Subresources.m_Aspects);
				const auto& footprint = copy.m_Footprint;
				if (!expected.IsValid() || footprint.m_Format != expected.m_Format || footprint.m_Width != expected.m_Width ||
					footprint.m_Height != expected.m_Height || footprint.m_BytesPerTexel != expected.m_BytesPerTexel ||
					footprint.m_RowSizeInBytes != expected.m_RowSizeInBytes || footprint.m_RowPitch != expected.m_RowPitch ||
					footprint.m_SizeInBytes != expected.m_SizeInBytes || copy.m_SlicePitch < footprint.m_SizeInBytes ||
					copy.m_SlicePitch % RHITextureCopyPlacementAlignment != 0 ||
					copy.m_SlicePitch % expected.m_BytesPerTexel != 0 ||
					copy.m_SlicePitch > copy.m_ReadbackBytes / target.m_Subresources.m_ArraySliceCount)
				{
					return false;
				}
			}
		}
		std::vector<RGObservationScan> resolvedScans(scans.begin(), scans.end());
		for (auto& scan : resolvedScans)
		{
			const auto target = ResolveObservation(scan.m_Target.m_Address, RGObservationKind::NonFiniteScan);
			if (scan.m_Target.m_Failure != RGObservationFailure::None ||
				scan.m_Target.m_Kind != RGObservationKind::NonFiniteScan ||
				target.m_Failure != RGObservationFailure::None || target.m_Node != scan.m_Target.m_Node ||
				target.m_Resource != scan.m_Target.m_Resource || !scan.m_Readback.IsValid() ||
				!m_Device->IsAlive(scan.m_Readback) || scan.m_RecordBytes == 0 || scan.m_RecordBytes % 4 != 0 ||
				!scan.m_Clear || !scan.m_Scan || (target.m_Type == RGResourceType::RGBuffer &&
					(!scan.m_Components.IsValid() || !m_Device->IsAlive(scan.m_Components) || scan.m_ComponentBytes == 0)))
			{
				return false;
			}
			if (target.m_Type == RGResourceType::RGTexture && (target.m_RawTypeless ||
				!m_Device->QueryTextureViewSupport(target.m_TextureDesc, {
					.m_Type = RHITextureViewType::ShaderResource, .m_Dimension = RHITextureViewDimension::Texture2DArray,
					.m_Format = target.m_ViewFormat, .m_Subresources = target.m_Subresources }).IsSupported()))
			{
				return false;
			}
			scan.m_Target = target;
		}
		if (counters)
		{
			if (!counters->m_Clear || !m_Device->IsAlive(counters->m_Readback) || counters->m_Bytes == 0)
			{
				return false;
			}
			struct Data
			{
				RGBufferId m_Record;
				RGBufferId m_Readback;
				RGProductionCounters m_Config;
			};
			RGBufferId record;
			const uint32_t clearIndex = static_cast<uint32_t>(m_PassNodes.size());
			AddPass<Data>("Observation.ProductionCounters.Clear", RGPassEncoderType::Compute,
				[&](RGBuilder& builder, Data& data)
				{
					data.m_Config = *counters;
					record = builder.CreateBuffer("Observation.ProductionCounters.Record", { .m_SizeInBytes = counters->m_Bytes, .m_StrideInBytes = 4 });
					data.m_Record = builder.Write(record, RGBufferAccess::StorageWrite);
					record = data.m_Record;
				}, [](RGExecuteContext& context, Data& data) { data.m_Config.m_Clear(context, data.m_Record); });
			const auto originalOrder = m_ExecutionPlan->GetExecutionOrder();
			for (const auto pass : originalOrder)
			{
				if (m_PassNodes[pass.Value()].m_EncoderType == RGPassEncoderType::Copy)
				{
					continue;
				}
				RGBuilder builder(*this, pass);
				record = builder.ReadWrite(record, RGBufferAccess::StorageReadWrite);
				m_PassNodes[pass.Value()].m_PassStorageBuffer = record;
				m_PassNodes[pass.Value()].m_PassStorageBinding = counters->m_Binding;
			}
			const uint32_t copyIndex = static_cast<uint32_t>(m_PassNodes.size());
			AddPass<Data>("Observation.ProductionCounters.Readback", RGPassEncoderType::Copy,
				[&](RGBuilder& builder, Data& data)
				{
					data.m_Config = *counters;
					data.m_Record = builder.Read(record, RGBufferAccess::CopySource);
					data.m_Readback = builder.ImportBuffer("Observation.ProductionCounters.Readback", counters->m_Readback,
						{ .m_SizeInBytes = counters->m_Bytes, .m_Usage = RHIBufferUsage::CopyDest, .m_MemoryUsage = RHIMemoryUsage::GpuToCpu },
						RGBufferAccess::CopyDest, RGContentValidity::Undefined);
					builder.WriteInPlace(data.m_Readback, RGBufferAccess::CopyDest);
				},
				[](RGExecuteContext& context, Data& data)
				{
					context.GetCopyCommandContext()->CopyBuffer(context.GetBufferHandle(data.m_Readback), 0,
						context.GetBufferHandle(data.m_Record), 0, data.m_Config.m_Bytes);
				});
			RGObservationTarget target{ .m_Address = { .m_Resource = "Observation.ProductionCounters.Record" },
				.m_Kind = RGObservationKind::ProductionCounters,
				.m_Type = RGResourceType::RGBuffer,
				.m_Resource = record,
				.m_Node = m_ResourceSlots[record.GetHandle().Value()].m_ResourceNodeIndex };
			m_ObservationPasses.emplace_back(clearIndex, target);
			target.m_InsertionPosition = static_cast<uint32_t>(originalOrder.size());
			m_ObservationPasses.emplace_back(copyIndex, target);
		}
		for (const auto& copy : resolved)
		{
			struct Data
			{
				RGBufferId m_Readback;
				RGObservationCopy m_Copy;
			};
			const uint32_t index = static_cast<uint32_t>(m_PassNodes.size());
			const std::string name = std::format("Observation.ResourceExport.{}", index);
			const std::string bufferName = std::format("{}.Readback", name);
			AddPass<Data>(name.c_str(), RGPassEncoderType::Copy,
				[&](RGBuilder& builder, Data& data)
				{
					data.m_Copy = copy;
					data.m_Readback = builder.ImportBuffer(bufferName.c_str(), copy.m_Readback,
						{ .m_SizeInBytes = copy.m_ReadbackBytes, .m_Usage = RHIBufferUsage::CopyDest,
							.m_MemoryUsage = RHIMemoryUsage::GpuToCpu }, RGBufferAccess::CopyDest, RGContentValidity::Undefined);
					builder.WriteInPlace(data.m_Readback, RGBufferAccess::CopyDest);
				},
				[](RGExecuteContext& context, Data& data)
				{
					auto* commands = context.GetCopyCommandContext();
					const auto destination = context.GetBufferHandle(data.m_Readback);
					if (data.m_Copy.m_Target.m_Type == RGResourceType::RGBuffer)
					{
						commands->CopyBuffer(destination, 0, context.GetBufferHandle(RGBufferId{ *data.m_Copy.m_Target.m_Resource }), 0,
							data.m_Copy.m_Target.m_BufferDesc.m_SizeInBytes);
						return;
					}
					const auto& range = data.m_Copy.m_Target.m_Subresources;
					for (uint32_t slice = 0; slice < range.m_ArraySliceCount; ++slice)
					{
						commands->CopyTextureToBuffer({
							.m_Source = context.GetTextureHandle(RGTextureId{ *data.m_Copy.m_Target.m_Resource }),
							.m_SourceMipLevel = range.m_BaseMip,
							.m_SourceArraySlice = range.m_BaseArraySlice + slice,
							.m_Destination = destination,
							.m_DestinationOffset = slice * data.m_Copy.m_SlicePitch,
							.m_Footprint = data.m_Copy.m_Footprint,
							.m_SourceAspect = range.m_Aspects });
					}
				});
			// Dedicated stale-version read. The ordinary builder keeps its Read/Export assertions.
			m_PassNodes[index].m_Accesses.push_back({ .m_ResourceNodeIndex = copy.m_Target.m_Node,
				.m_AccessValue = copy.m_Target.m_Type == RGResourceType::RGTexture ?
					static_cast<uint64_t>(RGTextureAccess::CopySource) :
					static_cast<uint64_t>(RGBufferAccess::CopySource),
				.m_Stages = RHIStage::Copy,
				.m_ResourceType = copy.m_Target.m_Type,
				.m_DependencyAccess = RGDependencyAccess::Read,
				.m_Subresources = copy.m_Target.m_Type == RGResourceType::RGTexture ?
					std::optional(copy.m_Target.m_Subresources) :
					std::nullopt });
			m_ResourceNodes[copy.m_Target.m_Node.Value()].m_Readers.push_back(RGPassNodeIndex{ index });
			m_ObservationPasses.emplace_back(index, copy.m_Target);
		}
		for (const auto& scan : resolvedScans)
		{
			struct Data
			{
				RGBufferId m_Record;
				RGBufferId m_Readback;
				RGTextureViewId m_View;
				RGObservationScan m_Scan;
			};
			RGBufferId record;
			const uint32_t first = static_cast<uint32_t>(m_PassNodes.size());
			const std::string name = std::format("Observation.NonFiniteScan.{}", first);
			AddPass<Data>((name + ".Clear").c_str(), RGPassEncoderType::Compute,
				[&](RGBuilder& builder, Data& data)
				{
					data.m_Scan = scan;
					record = builder.CreateBuffer((name + ".Record").c_str(), {
						.m_SizeInBytes = scan.m_RecordBytes, .m_StrideInBytes = scan.m_RecordBytes });
					data.m_Record = builder.Write(record, RGBufferAccess::StorageWrite);
					record = data.m_Record;
				}, [](RGExecuteContext& context, Data& data) { data.m_Scan.m_Clear(context, data.m_Record); });
			AddPass<Data>(name.c_str(), RGPassEncoderType::Compute,
				[&](RGBuilder& builder, Data& data)
				{
					data.m_Scan = scan;
					data.m_Record = builder.ReadWrite(record, RGBufferAccess::StorageReadWrite);
					record = data.m_Record;
					if (scan.m_Target.m_Type == RGResourceType::RGTexture)
					{
						data.m_View = CreateTextureView<RHITextureViewType::ShaderResource>(RGPassNodeIndex{ first + 1 },
							RGTextureId{ *scan.m_Target.m_Resource }, RHITextureViewDesc{
								.m_Dimension = RHITextureViewDimension::Texture2DArray, .m_Format = scan.m_Target.m_ViewFormat,
								.m_Subresources = scan.m_Target.m_Subresources });
					}
					else
					{
						const auto components = builder.ImportBuffer((name + ".Components").c_str(), scan.m_Components,
							{ .m_SizeInBytes = scan.m_ComponentBytes, .m_StrideInBytes = 8, .m_Usage = RHIBufferUsage::Structured,
								.m_MemoryUsage = RHIMemoryUsage::CpuToGpu }, RGBufferAccess::StructuredRead, RGContentValidity::Defined);
						GGLAB_UNUSED(builder.Read(components, RGBufferAccess::StructuredRead));
					}
				}, [](RGExecuteContext& context, Data& data) { data.m_Scan.m_Scan(context, data.m_Record, data.m_View); });
			// Only this dedicated path may read the addressed, possibly stale version.
			m_PassNodes[first + 1].m_Accesses.push_back({
				.m_ResourceNodeIndex = scan.m_Target.m_Node,
				.m_AccessValue = scan.m_Target.m_Type == RGResourceType::RGTexture ?
					static_cast<uint64_t>(RGTextureAccess::Sample) :
					static_cast<uint64_t>(RGBufferAccess::StructuredRead),
				.m_Stages = RHIStage::ComputeShader,
				.m_ResourceType = scan.m_Target.m_Type,
				.m_DependencyAccess = RGDependencyAccess::Read,
				.m_Subresources = scan.m_Target.m_Type == RGResourceType::RGTexture ?
					std::optional(scan.m_Target.m_Subresources) :
					std::nullopt });
			m_ResourceNodes[scan.m_Target.m_Node.Value()].m_Readers.push_back(RGPassNodeIndex{ first + 1 });
			AddPass<Data>((name + ".Readback").c_str(), RGPassEncoderType::Copy,
				[&](RGBuilder& builder, Data& data)
				{
					data.m_Scan = scan;
					data.m_Record = builder.Read(record, RGBufferAccess::CopySource);
					data.m_Readback = builder.ImportBuffer((name + ".Readback").c_str(), scan.m_Readback,
						{ .m_SizeInBytes = scan.m_RecordBytes, .m_Usage = RHIBufferUsage::CopyDest,
							.m_MemoryUsage = RHIMemoryUsage::GpuToCpu }, RGBufferAccess::CopyDest, RGContentValidity::Undefined);
					builder.WriteInPlace(data.m_Readback, RGBufferAccess::CopyDest);
				},
				[](RGExecuteContext& context, Data& data)
				{
					context.GetCopyCommandContext()->CopyBuffer(context.GetBufferHandle(data.m_Readback), 0,
						context.GetBufferHandle(data.m_Record), 0, data.m_Scan.m_RecordBytes);
				});
			for (uint32_t i = first; i < first + 3; ++i)
			{
				m_ObservationPasses.emplace_back(i, scan.m_Target);
			}
		}
		auto extended = RGCompiler::ExtendObservations(*this, *m_ExecutionPlan);
		if (!extended)
		{
			return false;
		}
		m_ExecutionPlan = std::move(extended.m_Plan);
		return true;
	}
}
