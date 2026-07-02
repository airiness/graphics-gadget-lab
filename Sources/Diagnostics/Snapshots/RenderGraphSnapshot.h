#pragma once
#include "Diagnostics/SnapshotCommon.h"
#include "Graphics/RenderGraph/RGResourceUtils.h"
#include "Graphics/Resource/TransientResourcePool.h"

namespace gglab
{
	struct RGSnapshotAccessInfo
	{
		uint32_t m_ResourceNodeIndex = 0;
		uint32_t m_ResourceSlot = 0;
		uint16_t m_ResourceVersion = 0;
		uint32_t m_VirtualResourceIndex = 0;
		std::string m_ResourceName;
		RGResourceType m_ResourceType = RGResourceType::RGTexture;
		RGDependencyAccess m_DependencyAccess = RGDependencyAccess::Read;
		uint64_t m_AccessValue = 0;
		RHIStage m_Stages = RHIStage::None;
		std::optional<RHISubresourceRange> m_Subresources = std::nullopt;
	};

	struct RGSnapshotBarrierInfo
	{
		uint32_t m_VirtualResourceIndex = 0;
		std::string m_ResourceName;
		RGResourceType m_ResourceType = RGResourceType::RGTexture;
		RHIResourceState m_Before = CommonRHIResourceState();
		RHIResourceState m_After = CommonRHIResourceState();
		std::optional<RHISubresourceRange> m_Subresources = std::nullopt;
	};

	struct RGSnapshotPassInfo
	{
		uint32_t m_Index = 0;
		int32_t m_ExecutionOrder = -1;
		std::string m_Name;
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

	struct RGSnapshot
	{
		std::vector<RGSnapshotPassInfo> m_Passes;
		std::vector<RGSnapshotResourceInfo> m_Resources;
		std::vector<RGSnapshotResourceNodeInfo> m_ResourceNodes;
		std::vector<RGSnapshotDependencyEdge> m_DependencyEdges;
	};

	template<>
	struct SnapshotTraits<RGSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.RenderGraphSnapshot");
	};
}
