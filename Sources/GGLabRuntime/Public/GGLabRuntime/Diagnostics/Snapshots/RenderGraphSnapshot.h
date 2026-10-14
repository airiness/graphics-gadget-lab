#pragma once
#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraphTypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGCompileDiagnostic.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourceTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gglab
{
	struct RGSnapshotAccessInfo
	{
		uint32_t m_ResourceNodeIndex = 0;
		uint32_t m_ResourceSlot = 0;
		uint16_t m_ResourceVersion = 0;
		std::optional<uint32_t> m_WrittenResourceNodeIndex;
		std::optional<uint16_t> m_WrittenResourceVersion;
		uint32_t m_VirtualResourceIndex = 0;
		std::string m_ResourceName;
		RGResourceType m_ResourceType = RGResourceType::RGTexture;
		RGDependencyAccess m_DependencyAccess = RGDependencyAccess::Read;
		RGOrderingRequirement m_Ordering = RGOrderingRequirement::Ordered;
		uint64_t m_AccessValue = 0;
		RHIStage m_Stages = RHIStage::None;
		std::optional<RHISubresourceRange> m_Subresources = std::nullopt;
	};

	struct RGSnapshotBarrierInfo
	{
		uint32_t m_VirtualResourceIndex = 0;
		std::string m_ResourceName;
		RGResourceType m_ResourceType = RGResourceType::RGTexture;
		RGBarrierKind m_Kind = RGBarrierKind::Transition;
		RGBarrierReason m_Reason = RGBarrierReason::AccessTransition;
		bool m_HasPhysicalHandle = false;
		uint32_t m_PhysicalHandleIndex = 0;
		uint32_t m_PhysicalHandleGeneration = 0;
		RHIResourceState m_Before = CommonRHIResourceState();
		RHIResourceState m_After = CommonRHIResourceState();
		std::optional<RHISubresourceRange> m_Subresources = std::nullopt;
	};

	struct RGSnapshotPassInfo
	{
		uint32_t m_Index = 0;
		uint32_t m_Occurrence = 1;
		int32_t m_ExecutionOrder = -1;
		std::string m_Name;
		RGPassEncoderType m_EncoderType = RGPassEncoderType::Graphics;
		bool m_SideEffect = false;
		bool m_Culled = false;
		std::vector<int32_t> m_DependencyPassIndices;
		std::vector<int32_t> m_DependentPassIndices;
		std::vector<RGSnapshotAccessInfo> m_Accesses;
		std::vector<RGSnapshotBarrierInfo> m_PreBarriers;
		std::vector<RGSnapshotBarrierInfo> m_PostBarriers;
		std::vector<uint32_t> m_DevirtualizeResources;
		std::vector<uint32_t> m_DestroyResources;
	};

	struct RGSnapshotResourceInfo
	{
		uint32_t m_Index = 0;
		std::string m_Name;
		RGResourceType m_ResourceType = RGResourceType::RGTexture;
		bool m_Imported = false;
		bool m_Devirtualized = false;
		int32_t m_RefCount = 0;
		int32_t m_FirstUserPassIndex = -1;
		int32_t m_LastUserPassIndex = -1;
		uint64_t m_UsageBits = 0;
		RHIFormat m_TextureFormat = RHIFormat::Unknown;
		RHIExtent3D m_TextureExtent{};
		uint16_t m_TextureMipLevels = 1;
		uint16_t m_TextureArraySize = 1;
		uint16_t m_TextureSampleCount = 1;
		uint64_t m_BufferSizeInBytes = 0;
		uint32_t m_BufferStrideInBytes = 0;
		int32_t m_ExportPassIndex = -1;
		std::optional<RHIClearValue> m_TextureClearValue = std::nullopt;
		TransientResourcePoolSlot m_PoolSlot{};
		RHIResourceState m_InitialBarrierState = CommonRHIResourceState();
		bool m_HasFinalBarrierState = false;
		RHIResourceState m_FinalBarrierState = CommonRHIResourceState();
		std::optional<RHISubresourceRange> m_FinalBarrierSubresources = std::nullopt;
	};

	struct RGSnapshotResourceNodeInfo
	{
		uint32_t m_Index = 0;
		uint32_t m_ResourceSlot = 0;
		uint16_t m_ResourceVersion = 0;
		uint32_t m_VirtualResourceIndex = 0;
		std::string m_ResourceName;
		RGResourceType m_ResourceType = RGResourceType::RGTexture;
		int32_t m_WriterPassIndex = -1;
		int32_t m_PreviousResourceNodeIndex = -1;
		std::vector<int32_t> m_ReaderPassIndices;
	};

	struct RGSnapshotDependencyEdge
	{
		int32_t m_FromPassIndex = -1;
		int32_t m_ToPassIndex = -1;
		uint32_t m_ResourceNodeIndex = 0;
		std::string m_FromPassName;
		std::string m_ToPassName;
		std::string m_ResourceName;
		RGDependencyReason m_Reason = RGDependencyReason::WriterToReader;
	};

	// Physical identities are sampled during execution, before Release clears the allocation.
	struct RGSnapshotAllocation
	{
		uint32_t m_VirtualResourceIndex = 0;
		uint32_t m_PassIndex = 0;
		bool m_Imported = false;
		TransientResourcePoolSlot m_PoolSlot{};
		bool m_HasPhysicalHandle = false;
		uint32_t m_PhysicalHandleIndex = 0;
		uint32_t m_PhysicalHandleGeneration = 0;
	};

	struct RGSnapshotResolvedBarrier
	{
		uint32_t m_PassIndex = 0;
		bool m_Post = false;
		RGSnapshotBarrierInfo m_Barrier{};
	};

	struct RGSnapshotExecutionRecord
	{
		bool m_Recorded = false;
		std::vector<uint32_t> m_ExecutedPassIndices;
		std::vector<RGSnapshotAllocation> m_Allocations;
		std::vector<RGSnapshotResolvedBarrier> m_Barriers;
	};

	struct RGSnapshot
	{
		std::vector<RGSnapshotPassInfo> m_Passes;
		std::vector<RGSnapshotResourceInfo> m_Resources;
		std::vector<RGSnapshotResourceNodeInfo> m_ResourceNodes;
		std::vector<RGSnapshotDependencyEdge> m_DependencyEdges;
		std::vector<RGCompileDiagnostic> m_CompileDiagnostics;
		std::vector<std::string> m_DuplicatePassNames;
		RGSnapshotExecutionRecord m_Execution;
	};

	// Versioned graph data; AppRuntime embeds its common frame metadata and submission identity.
	[[nodiscard]] std::string SerializeRenderGraphSnapshot(
		const RGSnapshot& snapshot, bool summary = false) noexcept;

	template <> struct SnapshotTraits<RGSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.RenderGraphSnapshot");
	};
}
