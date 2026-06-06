#pragma once
#include "Graphics/RenderGraph/RGResourceUtils.h"
#include "Graphics/GraphicsTypes.h"

namespace gglab
{
	class RenderGraph;

	struct RGSnapshotAccessInfo
	{
		uint32_t m_ResourceNodeIndex = 0;
		uint32_t m_ResourceSlot = 0;
		uint16_t m_ResourceVersion = 0;
		uint32_t m_VirtualResourceIndex = 0;
		std::string m_ResourceName;
		RGResourceType m_ResourceType = RGResourceType::RGTexture;
		RGResourceAccessType m_AccessType = RGResourceAccessType::Read;
		uint64_t m_UsageBits = 0;
	};

	struct RGSnapshotBarrierInfo
	{
		uint32_t m_VirtualResourceIndex = 0;
		std::string m_ResourceName;
		RGResourceType m_ResourceType = RGResourceType::RGTexture;
		RGBarrierState m_Before = CommonRGBarrierState();
		RGBarrierState m_After = CommonRGBarrierState();
	};

	struct RGSnapshotPassInfo
	{
		uint32_t m_Index = 0;
		int32_t m_ExecutionOrder = -1;
		std::string m_Name;
		bool m_SideEffect = false;
		bool m_Culled = false;
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
		ResourceIndex m_GpuResourceIndex{};
		RGBarrierState m_InitialBarrierState = CommonRGBarrierState();
		bool m_HasFinalBarrierState = false;
		RGBarrierState m_FinalBarrierState = CommonRGBarrierState();
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
	};

	struct RGSnapshot
	{
		std::vector<RGSnapshotPassInfo> m_Passes;
		std::vector<RGSnapshotResourceInfo> m_Resources;
		std::vector<RGSnapshotResourceNodeInfo> m_ResourceNodes;
		std::vector<RGSnapshotDependencyEdge> m_DependencyEdges;
	};

	void BuildRenderGraphSnapshot(const RenderGraph& rg, RGSnapshot& outSnapshot) noexcept;
}
