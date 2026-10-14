#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Diagnostics/RenderGraphEnumText.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"

#include <algorithm>
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
			return { {"stages", RGFlagsId(state.m_Stages)},
				{"stageBits", static_cast<uint64_t>(state.m_Stages)},
				{"access", RGFlagsId(state.m_Access)},
				{"accessBits", static_cast<uint64_t>(state.m_Access)},
				{"layout", RGEnumId(state.m_Layout)} };
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
			return { {"baseMip", range->m_BaseMip}, {"mipCount", count(range->m_MipCount)},
				{"baseSlice", range->m_BaseArraySlice}, {"sliceCount", count(range->m_ArraySliceCount)},
				{"aspects", RGFlagsId(range->m_Aspects)} };
		}

		Json Physical(bool valid, uint32_t index, uint32_t generation)
		{
			return valid ? Json{ {"index", index}, {"generation", generation} } : Json(nullptr);
		}

		Json Barrier(const RGSnapshotBarrierInfo& barrier, bool resolved)
		{
			Json result = { {"resourceIndex", barrier.m_VirtualResourceIndex},
				{"resource", barrier.m_ResourceName}, {"type", RGEnumId(barrier.m_ResourceType)},
				{"kind", RGEnumId(barrier.m_Kind)}, {"reason", RGEnumId(barrier.m_Reason)},
				{"before", State(barrier.m_Before)}, {"after", State(barrier.m_After)},
				{"subresources", Subresources(barrier.m_Subresources)} };
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
			for (const auto& barrier : barriers) result.push_back(Barrier(barrier, false));
			return result;
		}

		Json Access(const RGSnapshotAccessInfo& access)
		{
			return { {"resourceNodeIndex", access.m_ResourceNodeIndex},
				{"resourceSlot", access.m_ResourceSlot}, {"version", access.m_ResourceVersion},
				{"writtenResourceNodeIndex", access.m_WrittenResourceNodeIndex
					? Json(*access.m_WrittenResourceNodeIndex) : Json(nullptr)},
				{"writtenVersion", access.m_WrittenResourceVersion
					? Json(*access.m_WrittenResourceVersion) : Json(nullptr)},
				{"resourceIndex", access.m_VirtualResourceIndex}, {"resource", access.m_ResourceName},
				{"type", RGEnumId(access.m_ResourceType)},
				{"dependencyAccess", RGEnumId(access.m_DependencyAccess)},
				{"ordering", RGEnumId(access.m_Ordering)},
				{"access", access.m_ResourceType == RGResourceType::RGTexture
					? RGEnumId(static_cast<RGTextureAccess>(access.m_AccessValue))
					: RGEnumId(static_cast<RGBufferAccess>(access.m_AccessValue))},
				{"accessBits", access.m_AccessValue}, {"stages", RGFlagsId(access.m_Stages)},
				{"stageBits", static_cast<uint64_t>(access.m_Stages)},
				{"subresources", Subresources(access.m_Subresources)} };
		}

		Json Address(const RGSnapshotPassInfo& pass, const RGSnapshotAccessInfo& access, bool write)
		{
			return { {"resource", access.m_ResourceName},
				{"at", {{write ? "after" : "before", pass.m_Name}}},
				{"occurrence", pass.m_Occurrence} };
		}

		std::string_view Severity(RGCompileDiagnosticSeverity severity)
		{
			switch (severity)
			{
			case RGCompileDiagnosticSeverity::Info: return "Info";
			case RGCompileDiagnosticSeverity::Warning: return "Warning";
			case RGCompileDiagnosticSeverity::Error: return "Error";
			}
			return "Unknown";
		}

		std::string_view Code(RGCompileDiagnosticCode code)
		{
			switch (code)
			{
			case RGCompileDiagnosticCode::InvalidDeclaration: return "InvalidDeclaration";
			case RGCompileDiagnosticCode::UndefinedContentRead: return "UndefinedContentRead";
			case RGCompileDiagnosticCode::DependencyCycle: return "DependencyCycle";
			case RGCompileDiagnosticCode::InvalidExecutionPlan: return "InvalidExecutionPlan";
			}
			return "Unknown";
		}
	}

	std::string SerializeRenderGraphSnapshot(const RGSnapshot& snapshot, bool summary) noexcept
	{
		Json root = { {"schema", "gglab.render-graph"}, {"schemaVersion", std::ranges::any_of(snapshot.m_ObservationNodes, [](const auto& entry)
			{ return entry.second.m_Kind == RGObservationKind::NonFiniteScan; }) ? 3 : snapshot.m_ObservationNodes.empty() ? 1 : 2},
			{"mode", summary ? "summary" : "full"} };
		Json compile = { {"duplicatePassNames", snapshot.m_DuplicatePassNames} };
		Json passes = Json::array();
		for (const auto& pass : snapshot.m_Passes)
		{
			if (summary && (pass.m_Culled || pass.m_ExecutionOrder < 0))
			{
				continue;
			}
			Json item = { {"index", pass.m_Index}, {"name", pass.m_Name},
				{"occurrence", pass.m_Occurrence}, {"executionOrder", pass.m_ExecutionOrder}, {"observation", pass.m_Observation} };
			if (summary)
			{
				item["reads"] = Json::array();
				item["writes"] = Json::array();
				for (const auto& access : pass.m_Accesses)
				{
					Json value = { {"version", access.m_ResourceVersion},
						{"resourceNodeIndex", access.m_ResourceNodeIndex},
						{"subresources", Subresources(access.m_Subresources)} };
					if (access.m_DependencyAccess != RGDependencyAccess::Write)
					{
						value["address"] = Address(pass, access, false);
						item["reads"].push_back(value);
					}
					if (access.m_DependencyAccess != RGDependencyAccess::Read)
					{
						// ReadWrite reads the incoming node but writes the version produced by this pass.
						value["version"] = access.m_WrittenResourceVersion
							? Json(*access.m_WrittenResourceVersion) : Json(nullptr);
						value["resourceNodeIndex"] = access.m_WrittenResourceNodeIndex
							? Json(*access.m_WrittenResourceNodeIndex) : Json(nullptr);
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
				for (const auto& access : pass.m_Accesses) item["accesses"].push_back(Access(access));
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
				{ return a["executionOrder"].get<int32_t>() < b["executionOrder"].get<int32_t>(); });
		}
		compile["passes"] = std::move(passes);
		if (!summary)
		{
			compile["resources"] = Json::array();
			for (const auto& resource : snapshot.m_Resources)
			{
				Json item = { {"index", resource.m_Index}, {"name", resource.m_Name},
					{"type", RGEnumId(resource.m_ResourceType)}, {"imported", resource.m_Imported},
					{"referenceCount", resource.m_RefCount}, {"firstUser", resource.m_FirstUserPassIndex},
					{"lastUser", resource.m_LastUserPassIndex}, {"usageBits", resource.m_UsageBits},
					{"usage", resource.m_ResourceType == RGResourceType::RGTexture
						? RGFlagsId(static_cast<RHITextureUsage>(resource.m_UsageBits))
						: RGFlagsId(static_cast<RHIBufferUsage>(resource.m_UsageBits))},
					{"initialState", State(resource.m_InitialBarrierState)},
					{"finalState", resource.m_HasFinalBarrierState ? State(resource.m_FinalBarrierState) : Json(nullptr)},
					{"finalSubresources", Subresources(resource.m_FinalBarrierSubresources)},
					{"exportPass", resource.m_ExportPassIndex} };
				if (resource.m_ResourceType == RGResourceType::RGTexture)
				{
					item["texture"] = { {"format", GetRHIFormatInfo(resource.m_TextureFormat).m_Name},
						{"extent", {resource.m_TextureExtent.m_Width, resource.m_TextureExtent.m_Height,
							resource.m_TextureExtent.m_Depth}}, {"mips", resource.m_TextureMipLevels},
						{"slices", resource.m_TextureArraySize}, {"samples", resource.m_TextureSampleCount} };
				}
				else
				{
					item["buffer"] = { {"sizeBytes", resource.m_BufferSizeInBytes},
						{"strideBytes", resource.m_BufferStrideInBytes} };
				}
				compile["resources"].push_back(std::move(item));
			}
			compile["versions"] = Json::array();
			for (const auto& node : snapshot.m_ResourceNodes)
			{
				compile["versions"].push_back({ {"index", node.m_Index}, {"resourceSlot", node.m_ResourceSlot},
					{"version", node.m_ResourceVersion}, {"resourceIndex", node.m_VirtualResourceIndex},
					{"resource", node.m_ResourceName}, {"type", RGEnumId(node.m_ResourceType)},
					{"writer", node.m_WriterPassIndex}, {"previous", node.m_PreviousResourceNodeIndex},
					{"readers", node.m_ReaderPassIndices} });
			}
			compile["dependencyEdges"] = Json::array();
			for (const auto& edge : snapshot.m_DependencyEdges)
			{
				compile["dependencyEdges"].push_back({ {"from", edge.m_FromPassIndex},
					{"to", edge.m_ToPassIndex}, {"resourceNodeIndex", edge.m_ResourceNodeIndex},
					{"reason", RGEnumId(edge.m_Reason)} });
			}
			compile["diagnostics"] = Json::array();
			for (const auto& diagnostic : snapshot.m_CompileDiagnostics)
			{
				compile["diagnostics"].push_back({ {"severity", Severity(diagnostic.m_Severity)},
					{"code", Code(diagnostic.m_Code)}, {"message", diagnostic.m_Message},
					{"passIndex", diagnostic.m_Pass.IsValid() ? Json(diagnostic.m_Pass.Value()) : Json(nullptr)},
					{"resourceIndex", diagnostic.m_Resource.IsValid() ? Json(diagnostic.m_Resource.Value()) : Json(nullptr)} });
			}
			const auto& record = snapshot.m_Execution;
			Json execution = { {"status", record.m_Recorded ? "recorded" : "unavailable"},
				{"executedPassIndices", record.m_ExecutedPassIndices}, {"allocations", Json::array()},
				{"resolvedBarriers", Json::array()}, {"observationNodes", Json::array()},
				{"gpuTiming", {{"status", "unavailable"},
					{"reason", "Backend scopes aggregate by name without individual pass identity."}}} };
			for (const auto& allocation : record.m_Allocations)
			{
				execution["allocations"].push_back({ {"resourceIndex", allocation.m_VirtualResourceIndex},
					{"passIndex", allocation.m_PassIndex}, {"imported", allocation.m_Imported},
					{"poolSlot", allocation.m_PoolSlot.IsValid() ? Json(allocation.m_PoolSlot.Value()) : Json(nullptr)},
					{"physical", Physical(allocation.m_HasPhysicalHandle, allocation.m_PhysicalHandleIndex,
						allocation.m_PhysicalHandleGeneration)} });
			}
			for (const auto& [index, target] : snapshot.m_ObservationNodes)
			{
				execution["observationNodes"].push_back({ {"passIndex", index}, {"kind", target.m_Kind == RGObservationKind::ResourceExport ? "ResourceExport" : "NonFiniteScan"},
					{"resource", target.m_Address.m_Resource}, {"resourceIndex", target.m_VirtualResource.Value()},
					{"resourceNodeIndex", target.m_Node.Value()}, {"originalInsertionPosition", target.m_InsertionPosition} });
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
