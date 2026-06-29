#include "Core/Precompiled.h"
#include "Graphics/RenderGraph/RenderGraphSnapshot.h"
#include "Graphics/RenderGraph/RenderGraph.h"

namespace gglab
{
	namespace
	{
		constexpr uint32_t InvalidSnapshotResourceIndex = std::numeric_limits<uint32_t>::max();

		static std::string ToSnapshotName(StringID nameId) noexcept
		{
			const std::string_view name = nameId.Name();
			if (!name.empty())
			{
				return std::string(name);
			}

			return std::format("0x{:016X}", nameId.Value());
		}

		static int32_t ToSnapshotPassIndex(RGPassNodeIndex passIndex) noexcept
		{
			return passIndex.IsValid() ? static_cast<int32_t>(passIndex.Value()) : -1;
		}

		static TransientResourcePoolSlot GetVirtualResourcePoolSlot(
			const RGVirtualResourceBase* virtualResource) noexcept
		{
			GGLAB_ASSERT_NOT_NULL(virtualResource);

			switch (virtualResource->m_ResourceType)
			{
			case RGResourceType::RGTexture:
				return static_cast<const RGVirtualResource<RGTextureResource>*>(virtualResource)
					->m_PhysicalAllocation.m_PoolSlot;
			case RGResourceType::RGBuffer:
				return static_cast<const RGVirtualResource<RGBufferResource>*>(virtualResource)
					->m_PhysicalAllocation.m_PoolSlot;
			}

			GGLAB_UNREACHABLE("Unhandled RGResourceType.");
		}

		static std::string GetPassSnapshotName(const std::vector<RGPassNode>& passNodes, int32_t passIndex) noexcept
		{
			if (passIndex < 0 || static_cast<size_t>(passIndex) >= passNodes.size())
			{
				return "External";
			}

			return ToSnapshotName(passNodes[passIndex].m_NameId);
		}

		class RGSnapshotBuilder
		{
		public:
			RGSnapshotBuilder(
				const std::vector<RGResourceNode>& resourceNodes,
				const std::vector<RGPassNode>& passNodes,
				const std::vector<RGPassDependencyEdge>& dependencyEdges,
				const std::vector<RGVirtualResourceBase*>& virtualResources,
				RGSnapshot& outSnapshot) noexcept :
				m_ResourceNodes(resourceNodes),
				m_PassNodes(passNodes),
				m_DependencyEdges(dependencyEdges),
				m_VirtualResources(virtualResources),
				m_OutSnapshot(outSnapshot)
			{}

			void Build() noexcept
			{
				m_OutSnapshot = {};
				m_OutSnapshot.m_Passes.reserve(m_PassNodes.size());
				m_OutSnapshot.m_Resources.reserve(m_VirtualResources.size());
				m_OutSnapshot.m_ResourceNodes.reserve(m_ResourceNodes.size());

				BuildVirtualResourceIndices();
				BuildPasses();
				BuildResources();
				BuildResourceNodesAndDependencies();
			}

		private:
			void BuildVirtualResourceIndices() noexcept
			{
				m_VirtualResourceIndices.reserve(m_VirtualResources.size());
				for (uint32_t resourceIndex = 0; resourceIndex < m_VirtualResources.size(); ++resourceIndex)
				{
					m_VirtualResourceIndices.emplace(m_VirtualResources[resourceIndex], resourceIndex);
				}
			}

			void BuildPasses() noexcept
			{
				for (uint32_t passIndex = 0; passIndex < m_PassNodes.size(); ++passIndex)
				{
					const auto& passNode = m_PassNodes[passIndex];

					RGSnapshotPassInfo passInfo = {};
					passInfo.m_Index = passIndex;
					passInfo.m_ExecutionOrder = passNode.m_ExecutionOrder;
					passInfo.m_Name = ToSnapshotName(passNode.m_NameId);
					passInfo.m_SideEffect = passNode.m_SideEffect;
					passInfo.m_Culled = passNode.m_Culled;
					passInfo.m_Accesses.reserve(passNode.m_Accesses.size());
					passInfo.m_PreBarriers.reserve(passNode.m_PreBarriers.size());
					passInfo.m_PostBarriers.reserve(passNode.m_PostBarriers.size());
					passInfo.m_DevirtualizeResources.reserve(passNode.m_DevirtualizeVirtualResources.size());
					passInfo.m_DestroyResources.reserve(passNode.m_DestroyVirtualResources.size());

					for (const auto& access : passNode.m_Accesses)
					{
						passInfo.m_Accesses.push_back(BuildAccessInfo(access));
					}

					for (const auto& barrier : passNode.m_PreBarriers)
					{
						passInfo.m_PreBarriers.push_back(BuildBarrierInfo(barrier));
					}

					for (const auto& barrier : passNode.m_PostBarriers)
					{
						passInfo.m_PostBarriers.push_back(BuildBarrierInfo(barrier));
					}

					for (const auto* virtualResource : passNode.m_DevirtualizeVirtualResources)
					{
						passInfo.m_DevirtualizeResources.push_back(LookupVirtualResourceIndex(virtualResource));
					}

					for (const auto* virtualResource : passNode.m_DestroyVirtualResources)
					{
						passInfo.m_DestroyResources.push_back(LookupVirtualResourceIndex(virtualResource));
					}

					m_OutSnapshot.m_Passes.push_back(std::move(passInfo));
				}
			}

			void BuildResources() noexcept
			{
				for (uint32_t resourceIndex = 0; resourceIndex < m_VirtualResources.size(); ++resourceIndex)
				{
					const auto* virtualResource = m_VirtualResources[resourceIndex];
					GGLAB_ASSERT_NOT_NULL(virtualResource);

					RGSnapshotResourceInfo resourceInfo = {};
					resourceInfo.m_Index = resourceIndex;
					resourceInfo.m_Name = ToSnapshotName(virtualResource->m_NameId);
					resourceInfo.m_ResourceType = virtualResource->m_ResourceType;
					resourceInfo.m_Imported = virtualResource->m_Imported;
					resourceInfo.m_Devirtualized = virtualResource->m_Devirtualized;
					resourceInfo.m_RefCount = virtualResource->m_RefCount;
					resourceInfo.m_FirstUserPassIndex = ToSnapshotPassIndex(virtualResource->m_FirstUser);
					resourceInfo.m_LastUserPassIndex = ToSnapshotPassIndex(virtualResource->m_LastUser);
					resourceInfo.m_UsageBits = virtualResource->GetCompiledUsageBits();
					resourceInfo.m_PoolSlot = GetVirtualResourcePoolSlot(virtualResource);
					resourceInfo.m_InitialBarrierState = virtualResource->m_InitialBarrierState;
					resourceInfo.m_HasFinalBarrierState = virtualResource->m_FinalBarrierState.has_value();
					resourceInfo.m_FinalBarrierState = virtualResource->m_FinalBarrierState.value_or(CommonRHIResourceState());
					m_OutSnapshot.m_Resources.push_back(std::move(resourceInfo));
				}
			}

			void BuildResourceNodesAndDependencies() noexcept
			{
				for (uint32_t resourceNodeIndex = 0; resourceNodeIndex < m_ResourceNodes.size(); ++resourceNodeIndex)
				{
					const auto& resourceNode = m_ResourceNodes[resourceNodeIndex];

					RGSnapshotResourceNodeInfo resourceNodeInfo = {};
					resourceNodeInfo.m_Index = resourceNodeIndex;
					resourceNodeInfo.m_ResourceSlot = resourceNode.m_ResourceHandle.GetHandle().Value();
					resourceNodeInfo.m_ResourceVersion = resourceNode.m_ResourceHandle.GetVersion();
					resourceNodeInfo.m_VirtualResourceIndex = LookupVirtualResourceIndex(resourceNode.m_VirtualResource);
					resourceNodeInfo.m_ResourceName = ToSnapshotName(resourceNode.NameId());
					resourceNodeInfo.m_ResourceType = resourceNode.m_VirtualResource ?
						resourceNode.m_VirtualResource->m_ResourceType :
						RGResourceType::RGTexture;
					resourceNodeInfo.m_WriterPassIndex = ToSnapshotPassIndex(resourceNode.m_Writer);
					resourceNodeInfo.m_PreviousResourceNodeIndex = resourceNode.m_Previous.IsValid() ?
						static_cast<int32_t>(resourceNode.m_Previous.Value()) :
						-1;
					resourceNodeInfo.m_ReaderPassIndices.reserve(resourceNode.m_Readers.size());
					for (const auto readerPassIndex : resourceNode.m_Readers)
					{
						resourceNodeInfo.m_ReaderPassIndices.push_back(ToSnapshotPassIndex(readerPassIndex));
					}
					m_OutSnapshot.m_ResourceNodes.push_back(std::move(resourceNodeInfo));

				}

				m_OutSnapshot.m_DependencyEdges.reserve(m_DependencyEdges.size());
				for (const auto& edge : m_DependencyEdges)
				{
					m_OutSnapshot.m_DependencyEdges.push_back(BuildDependencyEdge(edge));
				}
			}

			RGSnapshotAccessInfo BuildAccessInfo(const RGPassNode::Access& access) const noexcept
			{
				const auto& resourceNode = m_ResourceNodes[access.m_ResourceNodeIndex.Value()];

				RGSnapshotAccessInfo accessInfo = {};
				accessInfo.m_ResourceNodeIndex = access.m_ResourceNodeIndex.Value();
				accessInfo.m_ResourceSlot = resourceNode.m_ResourceHandle.GetHandle().Value();
				accessInfo.m_ResourceVersion = resourceNode.m_ResourceHandle.GetVersion();
				accessInfo.m_VirtualResourceIndex = LookupVirtualResourceIndex(resourceNode.m_VirtualResource);
				accessInfo.m_ResourceName = ToSnapshotName(resourceNode.NameId());
				accessInfo.m_ResourceType = access.m_ResourceType;
				accessInfo.m_DependencyAccess = access.m_DependencyAccess;
				accessInfo.m_AccessValue = access.m_AccessValue;
				accessInfo.m_Subresources = access.m_Subresources;
				return accessInfo;
			}

			RGSnapshotBarrierInfo BuildBarrierInfo(const RGPassNode::BarrierIntent& intent) const noexcept
			{
				GGLAB_ASSERT_NOT_NULL(intent.m_VirtualResource);

				RGSnapshotBarrierInfo info = {};
				info.m_VirtualResourceIndex = LookupVirtualResourceIndex(intent.m_VirtualResource);
				info.m_ResourceName = ToSnapshotName(intent.m_VirtualResource->m_NameId);
				info.m_ResourceType = intent.m_VirtualResource->m_ResourceType;
				info.m_Before = intent.m_Before;
				info.m_After = intent.m_After;
				info.m_Subresources = intent.m_Subresources;
				return info;
			}

			RGSnapshotDependencyEdge BuildDependencyEdge(const RGPassDependencyEdge& dependencyEdge) const noexcept
			{
				const RGResourceNode& resourceNode = m_ResourceNodes[dependencyEdge.m_ResourceNodeIndex.Value()];
				RGSnapshotDependencyEdge edge = {};
				edge.m_FromPassIndex = ToSnapshotPassIndex(dependencyEdge.m_From);
				edge.m_ToPassIndex = ToSnapshotPassIndex(dependencyEdge.m_To);
				edge.m_ResourceNodeIndex = dependencyEdge.m_ResourceNodeIndex.Value();
				edge.m_FromPassName = GetPassSnapshotName(m_PassNodes, edge.m_FromPassIndex);
				edge.m_ToPassName = GetPassSnapshotName(m_PassNodes, edge.m_ToPassIndex);
				edge.m_ResourceName = ToSnapshotName(resourceNode.NameId());
				return edge;
			}

			uint32_t LookupVirtualResourceIndex(const RGVirtualResourceBase* virtualResource) const noexcept
			{
				if (!virtualResource)
				{
					return InvalidSnapshotResourceIndex;
				}

				auto iter = m_VirtualResourceIndices.find(virtualResource);
				return iter != m_VirtualResourceIndices.end() ? iter->second : InvalidSnapshotResourceIndex;
			}

		private:
			const std::vector<RGResourceNode>& m_ResourceNodes;
			const std::vector<RGPassNode>& m_PassNodes;
			const std::vector<RGPassDependencyEdge>& m_DependencyEdges;
			const std::vector<RGVirtualResourceBase*>& m_VirtualResources;
			RGSnapshot& m_OutSnapshot;
			std::unordered_map<const RGVirtualResourceBase*, uint32_t> m_VirtualResourceIndices;
		};
	}

	void BuildRenderGraphSnapshot(const RenderGraph& rg, RGSnapshot& outSnapshot) noexcept
	{
		RGSnapshotBuilder(
			rg.m_ResourceNodes,
			rg.m_PassNodes,
			rg.m_DependencyEdges,
			rg.m_VirtualResources,
			outSnapshot).Build();
	}
}
