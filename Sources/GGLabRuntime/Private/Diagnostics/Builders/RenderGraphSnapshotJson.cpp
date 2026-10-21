#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Diagnostics/RenderGraphEnumText.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace gglab
{
	namespace
	{
		using Json = nlohmann::ordered_json;
		using diagnostics::RGEnumId;
		using diagnostics::RGFlagsId;

		Json State(const RHIResourceState& state)
		{
			return {
				{ "stages", RGFlagsId(state.m_Stages) },
				{ "stageBits", static_cast<uint64_t>(state.m_Stages) },
				{ "access", RGFlagsId(state.m_Access) },
				{ "accessBits", static_cast<uint64_t>(state.m_Access) },
				{ "layout", RGEnumId(state.m_Layout) } };
		}

		Json Subresources(const std::optional<RHISubresourceRange>& range)
		{
			if (!range)
			{
				return nullptr;
			}
			const auto count = [](uint32_t value)
				{
					return value == RHISubresourceRange::Remaining ? Json("remaining") : Json(value);
				};
			return {
				{ "baseMip", range->m_BaseMip },
				{ "mipCount", count(range->m_MipCount) },
				{ "baseSlice", range->m_BaseArraySlice },
				{ "sliceCount", count(range->m_ArraySliceCount) },
				{ "aspects", RGFlagsId(range->m_Aspects) } };
		}

		Json Physical(bool valid, uint32_t index, uint32_t generation)
		{
			return valid ? Json{ { "index", index },{ "generation", generation } } : Json(nullptr);
		}

		Json Barrier(const RGSnapshotBarrierInfo& barrier, bool resolved)
		{
			Json result = {
				{ "resourceIndex", barrier.m_VirtualResourceIndex },
				{ "resource", barrier.m_ResourceName },
				{ "type", RGEnumId(barrier.m_ResourceType) },
				{ "kind", RGEnumId(barrier.m_Kind) },
				{ "reason", RGEnumId(barrier.m_Reason) },
				{ "before", State(barrier.m_Before) },
				{ "after", State(barrier.m_After) },
				{ "subresources", Subresources(barrier.m_Subresources) } };
			if (resolved)
			{
				result["physical"] = Physical(barrier.m_HasPhysicalHandle,
					barrier.m_PhysicalHandleIndex, barrier.m_PhysicalHandleGeneration);
			}
			return result;
		}

		Json Barriers(const std::vector<RGSnapshotBarrierInfo>& barriers)
		{
			Json result = Json::array();
			for (const auto& barrier : barriers)
			{
				result.push_back(Barrier(barrier, false));
			}
			return result;
		}

		Json Access(const RGSnapshotAccessInfo& access)
		{
			return {
				{ "resourceNodeIndex", access.m_ResourceNodeIndex },
				{ "resourceSlot", access.m_ResourceSlot },
				{ "version", access.m_ResourceVersion },
				{ "writtenResourceNodeIndex", access.m_WrittenResourceNodeIndex ?
					Json(*access.m_WrittenResourceNodeIndex) :
					Json(nullptr) },
				{ "writtenVersion", access.m_WrittenResourceVersion ?
					Json(*access.m_WrittenResourceVersion) :
					Json(nullptr) },
				{ "resourceIndex", access.m_VirtualResourceIndex },
				{ "resource", access.m_ResourceName },
				{ "type", RGEnumId(access.m_ResourceType) },
				{ "dependencyAccess", RGEnumId(access.m_DependencyAccess) },
				{ "ordering", RGEnumId(access.m_Ordering) },
				{ "access", access.m_ResourceType == RGResourceType::RGTexture ?
					RGEnumId(static_cast<RGTextureAccess>(access.m_AccessValue)) :
					RGEnumId(static_cast<RGBufferAccess>(access.m_AccessValue)) },
				{ "accessBits", access.m_AccessValue },
				{ "stages", RGFlagsId(access.m_Stages) },
				{ "stageBits", static_cast<uint64_t>(access.m_Stages) },
				{ "subresources", Subresources(access.m_Subresources) } };
		}

		Json Address(const RGSnapshotPassInfo& pass, const RGSnapshotAccessInfo& access, bool write)
		{
			return { { "resource", access.m_ResourceName },
				{ "at",{ { write ? "after" : "before", pass.m_Name } } },
				{ "occurrence", pass.m_Occurrence } };
		}

		std::string_view ObservationKind(RGObservationKind kind)
		{
			switch (kind)
			{
			case RGObservationKind::ResourceExport:
				return "ResourceExport";
			case RGObservationKind::NonFiniteScan:
				return "NonFiniteScan";
			case RGObservationKind::ProductionCounters:
				return "ProductionCounters";
			}
			return "Unknown";
		}

		// Capabilities are read from the kinds present, never from the schema version.
		Json NodeKinds(const RGSnapshot& snapshot)
		{
			std::vector<std::string> kinds;
			for (const auto& entry : snapshot.m_ObservationNodes)
			{
				kinds.emplace_back(ObservationKind(entry.second.m_Kind));
			}
			std::ranges::sort(kinds);
			kinds.erase(std::unique(kinds.begin(), kinds.end()), kinds.end());
			return kinds;
		}

		std::string_view Dimension(RHITextureDimension dimension)
		{
			switch (dimension)
			{
			case RHITextureDimension::Texture1D:
				return "Texture1D";
			case RHITextureDimension::Texture2D:
				return "Texture2D";
			case RHITextureDimension::Texture3D:
				return "Texture3D";
			}
			return "Unknown";
		}

		Json LogicalBytesReason(const RGSnapshotResourceInfo& resource)
		{
			if (resource.m_LogicalBytes)
			{
				return nullptr;
			}
			const RHIFormatInfo& format = GetRHIFormatInfo(resource.m_TextureFormat);
			const bool described = format.m_BytesPerBlock != 0 && format.m_BlockWidth != 0 &&
				format.m_BlockHeight != 0;
			return described ? "size-overflow" : "unsupported-format";
		}

		std::string_view UnavailableReason(RHIAllocationFactsStatus status)
		{
			switch (status)
			{
			case RHIAllocationFactsStatus::Known:
				break;
			case RHIAllocationFactsStatus::External:
				return "external";
			case RHIAllocationFactsStatus::Unowned:
				return "unowned";
			case RHIAllocationFactsStatus::Unsupported:
				return "unsupported";
			}
			return "unsupported";
		}

		Json MemoryClass(RHIMemoryClass memoryClass)
		{
			switch (memoryClass)
			{
			case RHIMemoryClass::Unknown:
				break;
			case RHIMemoryClass::DeviceLocal:
				return "device-local";
			case RHIMemoryClass::Other:
				return "other";
			}
			return nullptr;
		}

		Json Allocation(const RGSnapshotAllocation& allocation)
		{
			const bool known = allocation.m_AllocationFacts == RHIAllocationFactsStatus::Known;
			const auto bytes = [known](uint64_t value) { return known ? Json(value) : Json(nullptr); };
			return {
				{ "resourceIndex", allocation.m_VirtualResourceIndex },
				{ "passIndex", allocation.m_PassIndex },
				{ "imported", allocation.m_Imported },
				{ "poolSlot", allocation.m_PoolSlot.IsValid() ? Json(allocation.m_PoolSlot.Value()) : Json(nullptr) },
				{ "physical", Physical(allocation.m_HasPhysicalHandle, allocation.m_PhysicalHandleIndex, allocation.m_PhysicalHandleGeneration) },
				{ "allocationFacts", known ? "known" : "unavailable" },
				{ "unavailableReason", known ? Json(nullptr) : Json(UnavailableReason(allocation.m_AllocationFacts)) },
				{ "allocatedBytes", bytes(allocation.m_AllocatedBytes) },
				{ "alignmentBytes", bytes(allocation.m_AlignmentBytes) },
				{ "memoryClass", known ? MemoryClass(allocation.m_MemoryClass) : Json(nullptr) },
				{ "nativeAllocation", known && allocation.m_NativeAllocation ? Json(*allocation.m_NativeAllocation) : Json(nullptr) },
				{ "offsetBytes", bytes(allocation.m_OffsetBytes) } };
		}

		// Derived from the frozen records only: logical sizes describe the data, allocation
		// bytes are allocator reports, and allocator bytes are process-wide context.
		Json Totals(const RGSnapshot& snapshot)
		{
			const auto& record = snapshot.m_Execution;
			uint64_t logicalBytes = 0;
			uint32_t unavailableLogical = 0;
			uint64_t allocationBytes = 0;
			uint32_t unavailableAllocations = 0;
			uint64_t uniqueBytes = 0;
			std::vector<bool> countedResources(snapshot.m_Resources.size());
			std::vector<bool> countedNativeAllocations(record.m_Allocations.size());
			for (const auto& allocation : record.m_Allocations)
			{
				const uint32_t resourceIndex = allocation.m_VirtualResourceIndex;
				if (resourceIndex < countedResources.size() && !countedResources[resourceIndex])
				{
					countedResources[resourceIndex] = true;
					const auto& logical = snapshot.m_Resources[resourceIndex].m_LogicalBytes;
					logicalBytes += logical.value_or(0);
					unavailableLogical += logical ? 0u : 1u;
				}
				if (allocation.m_AllocationFacts != RHIAllocationFactsStatus::Known)
				{
					++unavailableAllocations;
					continue;
				}
				allocationBytes += allocation.m_AllocatedBytes;
				const uint32_t ordinal = allocation.m_NativeAllocation.value_or(UINT32_MAX);
				if (ordinal < countedNativeAllocations.size() && !countedNativeAllocations[ordinal])
				{
					countedNativeAllocations[ordinal] = true;
					uniqueBytes += allocation.m_AllocatedBytes;
				}
			}
			const auto& allocator = record.m_AllocatorTotals;
			return {
				{ "logicalBytes", logicalBytes },
				{ "unavailableLogicalResources", unavailableLogical },
				{ "allocationBytes", allocationBytes },
				{ "unavailableAllocations", unavailableAllocations },
				{ "uniquePhysicalAllocationBytes", uniqueBytes },
				{ "allocatorBlockBytes", allocator.m_Known ? Json(allocator.m_BlockBytes) : Json(nullptr) },
				{ "allocatorAllocationBytes", allocator.m_Known ? Json(allocator.m_AllocationBytes) : Json(nullptr) } };
		}

		std::string_view Severity(RGCompileDiagnosticSeverity severity)
		{
			switch (severity)
			{
			case RGCompileDiagnosticSeverity::Info:
				return "Info";
			case RGCompileDiagnosticSeverity::Warning:
				return "Warning";
			case RGCompileDiagnosticSeverity::Error:
				return "Error";
			}
			return "Unknown";
		}

		std::string_view Code(RGCompileDiagnosticCode code)
		{
			switch (code)
			{
			case RGCompileDiagnosticCode::InvalidDeclaration:
				return "InvalidDeclaration";
			case RGCompileDiagnosticCode::UndefinedContentRead:
				return "UndefinedContentRead";
			case RGCompileDiagnosticCode::DependencyCycle:
				return "DependencyCycle";
			case RGCompileDiagnosticCode::InvalidExecutionPlan:
				return "InvalidExecutionPlan";
			}
			return "Unknown";
		}
	}

	std::string SerializeRenderGraphSnapshot(const RGSnapshot& snapshot, bool summary) noexcept
	{
		Json root = {
			{ "schema", "gglab.render-graph" },
			{ "schemaVersion", RenderGraphExportSchemaVersion },
			{ "mode", summary ? "summary" : "full" },
			{ "observation",{ { "nodeKinds", NodeKinds(snapshot) } } } };
		Json compile = { { "duplicatePassNames", snapshot.m_DuplicatePassNames } };
		Json passes = Json::array();
		for (const auto& pass : snapshot.m_Passes)
		{
			if (summary && (pass.m_Culled || pass.m_ExecutionOrder < 0))
			{
				continue;
			}
			Json item = {
				{ "index", pass.m_Index },
				{ "name", pass.m_Name },
				{ "occurrence", pass.m_Occurrence },
				{ "executionOrder", pass.m_ExecutionOrder },{ "observation", pass.m_Observation } };
			if (summary)
			{
				item["reads"] = Json::array();
				item["writes"] = Json::array();
				for (const auto& access : pass.m_Accesses)
				{
					Json value = {
						{ "version", access.m_ResourceVersion },
						{ "resourceNodeIndex", access.m_ResourceNodeIndex },
						{ "subresources", Subresources(access.m_Subresources) } };
					if (access.m_DependencyAccess != RGDependencyAccess::Write)
					{
						value["address"] = Address(pass, access, false);
						item["reads"].push_back(value);
					}
					if (access.m_DependencyAccess != RGDependencyAccess::Read)
					{
						// ReadWrite reads the incoming node but writes the version produced by this pass.
						value["version"] = access.m_WrittenResourceVersion ?
							Json(*access.m_WrittenResourceVersion) :
							Json(nullptr);
						value["resourceNodeIndex"] = access.m_WrittenResourceNodeIndex ?
							Json(*access.m_WrittenResourceNodeIndex) :
							Json(nullptr);
						value["address"] = Address(pass, access, true);
						item["writes"].push_back(value);
					}
				}
			}
			else
			{
				item["encoder"] = RGEnumId(pass.m_EncoderType);
				item["sideEffect"] = pass.m_SideEffect;
				item["culled"] = pass.m_Culled;
				item["dependencies"] = pass.m_DependencyPassIndices;
				item["dependents"] = pass.m_DependentPassIndices;
				item["accesses"] = Json::array();
				for (const auto& access : pass.m_Accesses)
				{
					item["accesses"].push_back(Access(access));
				}
				item["preBarriers"] = Barriers(pass.m_PreBarriers);
				item["postBarriers"] = Barriers(pass.m_PostBarriers);
				item["devirtualize"] = pass.m_DevirtualizeResources;
				item["destroy"] = pass.m_DestroyResources;
			}
			passes.push_back(std::move(item));
		}
		if (summary)
		{
			std::sort(passes.begin(), passes.end(), [](const Json& a, const Json& b)
				{
					return a["executionOrder"].get<int32_t>() < b["executionOrder"].get<int32_t>();
				});
		}
		compile["passes"] = std::move(passes);
		if (!summary)
		{
			compile["resources"] = Json::array();
			for (const auto& resource : snapshot.m_Resources)
			{
				Json item = {
					{ "index", resource.m_Index },
					{ "name", resource.m_Name },
					{ "type", RGEnumId(resource.m_ResourceType) },
					{ "imported", resource.m_Imported },
					{ "referenceCount", resource.m_RefCount },
					{ "firstUser", resource.m_FirstUserPassIndex },
					{ "lastUser", resource.m_LastUserPassIndex },
					{ "usageBits", resource.m_UsageBits },
					{ "usage", resource.m_ResourceType == RGResourceType::RGTexture ?
						RGFlagsId(static_cast<RHITextureUsage>(resource.m_UsageBits)) :
						RGFlagsId(static_cast<RHIBufferUsage>(resource.m_UsageBits)) },
					{ "initialState", State(resource.m_InitialBarrierState) },
					{ "finalState", resource.m_HasFinalBarrierState ? State(resource.m_FinalBarrierState) : Json(nullptr) },
					{ "finalSubresources", Subresources(resource.m_FinalBarrierSubresources) },
					{ "exportPass", resource.m_ExportPassIndex },
					{ "logicalBytes", resource.m_LogicalBytes ? Json(*resource.m_LogicalBytes) : Json(nullptr) },
					{ "logicalBytesReason", LogicalBytesReason(resource) } };
				if (resource.m_ResourceType == RGResourceType::RGTexture)
				{
					item["texture"] = {
						{ "dimension", Dimension(resource.m_TextureDimension) },
						{ "format", GetRHIFormatInfo(resource.m_TextureFormat).m_Name },
						{ "extent",{ resource.m_TextureExtent.m_Width, resource.m_TextureExtent.m_Height, resource.m_TextureExtent.m_Depth } },
						{ "mips", resource.m_TextureMipLevels },
						{ "slices", resource.m_TextureArraySize },
						{ "samples", resource.m_TextureSampleCount } };
				}
				else
				{
					item["buffer"] = {
						{ "sizeBytes", resource.m_BufferSizeInBytes },
						{ "strideBytes", resource.m_BufferStrideInBytes } };
				}
				compile["resources"].push_back(std::move(item));
			}
			compile["versions"] = Json::array();
			for (const auto& node : snapshot.m_ResourceNodes)
			{
				compile["versions"].push_back({
					{ "index", node.m_Index },
					{ "resourceSlot", node.m_ResourceSlot },
					{ "version", node.m_ResourceVersion },
					{ "resourceIndex", node.m_VirtualResourceIndex },
					{ "resource", node.m_ResourceName },
					{ "type", RGEnumId(node.m_ResourceType) },
					{ "writer", node.m_WriterPassIndex },
					{ "previous", node.m_PreviousResourceNodeIndex },
					{ "readers", node.m_ReaderPassIndices } });
			}
			compile["dependencyEdges"] = Json::array();
			for (const auto& edge : snapshot.m_DependencyEdges)
			{
				compile["dependencyEdges"].push_back({
					{ "from", edge.m_FromPassIndex },
					{ "to", edge.m_ToPassIndex },
					{ "resourceNodeIndex", edge.m_ResourceNodeIndex },
					{ "reason", RGEnumId(edge.m_Reason) } });
			}
			compile["diagnostics"] = Json::array();
			for (const auto& diagnostic : snapshot.m_CompileDiagnostics)
			{
				compile["diagnostics"].push_back({
					{ "severity", Severity(diagnostic.m_Severity) },
					{ "code", Code(diagnostic.m_Code) },
					{ "message", diagnostic.m_Message },
					{ "passIndex", diagnostic.m_Pass.IsValid() ? Json(diagnostic.m_Pass.Value()) : Json(nullptr) },
					{ "resourceIndex", diagnostic.m_Resource.IsValid() ? Json(diagnostic.m_Resource.Value()) : Json(nullptr) } });
			}
			const auto& record = snapshot.m_Execution;
			Json execution = {
				{ "status", record.m_Recorded ? "recorded" : "unavailable" },
				{ "executedPassIndices", record.m_ExecutedPassIndices },
				{ "allocations", Json::array() },
				{ "totals", Totals(snapshot) },
				{ "resolvedBarriers", Json::array() },
				{ "observationNodes", Json::array() },
				{ "gpuTiming",{ { "status", "unavailable" },
					{ "reason", "GPU timing is collected over a sequence, not a single graph export." } } } };
			for (const auto& allocation : record.m_Allocations)
			{
				execution["allocations"].push_back(Allocation(allocation));
			}
			for (const auto& [index, target] : snapshot.m_ObservationNodes)
			{
				execution["observationNodes"].push_back({
					{ "passIndex", index },
					{ "kind", ObservationKind(target.m_Kind) },
					{ "resource", target.m_Address.m_Resource },
					{ "resourceIndex", target.m_VirtualResource.Value() },
					{ "resourceNodeIndex", target.m_Node.Value() },
					{ "originalInsertionPosition", target.m_InsertionPosition } });
			}
			for (const auto& barrier : record.m_Barriers)
			{
				Json item = Barrier(barrier.m_Barrier, true);
				item["passIndex"] = barrier.m_PassIndex;
				item["phase"] = barrier.m_Post ? "post" : "pre";
				execution["resolvedBarriers"].push_back(std::move(item));
			}
			root["execution"] = std::move(execution);
		}
		root["compile"] = std::move(compile);
		return root.dump(2, ' ', false, Json::error_handler_t::replace);
	}
}
