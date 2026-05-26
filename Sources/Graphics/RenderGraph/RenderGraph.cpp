#include "Core/Precompiled.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGPass.h"
#include "Graphics/DX12/DX12Device.h"
#include "Graphics/DX12/DX12Texture.h"
#include "Graphics/DX12/DX12Buffer.h"
#include "Graphics/DX12/Cache/DX12ViewCache.h"

namespace gglab
{
	RenderGraph::RenderGraph(const CreateInfo& createInfo) noexcept :
		m_GpuResourceAllocator(createInfo.m_GpuResourceAllocator),
		m_ViewCache(createInfo.m_ViewCache),
		m_ExternalResourceRegistry(createInfo.m_ExternalResourceRegistry),
		m_ArenaAllocator(1u << 20),
		m_Blackboard(m_ArenaAllocator)
	{
		GGLAB_ASSERT_MSG(m_GpuResourceAllocator != nullptr, "GpuResourceAllocator can not be null.");
		GGLAB_ASSERT_MSG(m_ViewCache != nullptr, "DX12ViewCache can not be null.");
		GGLAB_ASSERT_MSG(m_ExternalResourceRegistry != nullptr, "RGExternalResourceRegistry can not be null.");
	}

	RenderGraph::~RenderGraph() noexcept = default;

	void RenderGraph::Compile() noexcept
	{
		std::vector<RGPassNodeIndex> stack;
		std::unordered_set<RGPassNodeIndex> reachable;

		auto addPass = [&stack, &reachable](RGPassNodeIndex passNodeIndex)
			{
				if (!passNodeIndex.IsValid())
				{
					return;
				}
				if (reachable.insert(passNodeIndex).second)
				{
					stack.push_back(passNodeIndex);
				}
			};

		for (uint32_t passNodeIndex = 0; passNodeIndex < m_PassNodes.size(); ++passNodeIndex)
		{
			auto& passNode = m_PassNodes[passNodeIndex];
			if (passNode.m_SideEffect)
			{
				addPass(RGPassNodeIndex{ passNodeIndex });
			}
		}

		while (!stack.empty())
		{
			const RGPassNodeIndex passNodeIndex = stack.back();
			stack.pop_back();

			RGPassNode& passNode = m_PassNodes[passNodeIndex.Value()];
			for (const auto& access : passNode.m_Accesses)
			{
				if (access.m_AccessType == RGPassNode::Access::Type::Read)
				{
					RGResourceNode& resourceNode = m_ResourceNodes[access.m_ResourceNodeIndex.Value()];
					if (resourceNode.m_Writer.IsValid())
					{
						addPass(resourceNode.m_Writer);
					}
				}
			}
		}

		for (uint32_t passNodeIndex = 0; passNodeIndex < m_PassNodes.size(); ++passNodeIndex)
		{
			auto& passNode = m_PassNodes[passNodeIndex];
			passNode.m_Culled = (reachable.find(RGPassNodeIndex{ passNodeIndex }) == reachable.end());

			passNode.m_DevirtualizeVirtualResources.clear();
			passNode.m_DestroyVirtualResources.clear();
		}

		for (auto& virtualResource : m_VirtualResources)
		{
			virtualResource->m_RefCount = 0;
			virtualResource->m_FirstUser = InvalidRGPassNodeIndex;
			virtualResource->m_LastUser = InvalidRGPassNodeIndex;
		}

		for (uint32_t passNodeIndex = 0; passNodeIndex < m_PassNodes.size(); ++passNodeIndex)
		{
			auto& passNode = m_PassNodes[passNodeIndex];
			if (passNode.m_Culled)
			{
				continue;
			}

			const RGPassNodeIndex stablePassNodeIndex{ passNodeIndex };
			auto markUse = [this, stablePassNodeIndex](RGResourceNode::Index index)
				{
					RGResourceNode& resourceNode = m_ResourceNodes[index.Value()];
					auto* virtualResource = resourceNode.m_VirtualResource;
					if (!virtualResource)
					{
						return;
					}
					++virtualResource->m_RefCount;
					if (!virtualResource->m_FirstUser.IsValid())
					{
						virtualResource->m_FirstUser = stablePassNodeIndex;
					}
					virtualResource->m_LastUser = stablePassNodeIndex;

				};

			for (const auto& access : passNode.m_Accesses)
			{
				markUse(access.m_ResourceNodeIndex);
			}
		}

		for (auto* virtualResource : m_VirtualResources)
		{
			if (virtualResource->m_RefCount == 0)
			{
				continue;
			}

			m_PassNodes[virtualResource->m_FirstUser.Value()].m_DevirtualizeVirtualResources.push_back(virtualResource);
			m_PassNodes[virtualResource->m_LastUser.Value()].m_DestroyVirtualResources.push_back(virtualResource);
		}
	}

	void RenderGraph::Execute(RGExecuteContext& executeContext) noexcept
	{
		for (auto& passNode : m_PassNodes)
		{
			if (passNode.m_Culled)
			{
				continue;
			}

			// Devirtualize Gpu resources
			for (auto* virtualResource : passNode.m_DevirtualizeVirtualResources)
			{
				virtualResource->Devirtualize(m_GpuResourceAllocator);
			}

			// Execute passes
			if (passNode.m_Pass)
			{
				passNode.m_Pass->Execute(executeContext);
			}

			for (auto* virtualResource : passNode.m_DestroyVirtualResources)
			{
				virtualResource->Destroy(*this);
			}
		}
	}

	void RenderGraph::Retire(const DX12FencePoint& fencePoint) noexcept
	{
		for (const auto texIndex : m_MarkedReleaseTextureIndices)
		{
			GGLAB_ASSERT_MSG(texIndex.IsValid(), "Releasing an invalid texture index.");
			m_ViewCache->RetireResourceAllViews(texIndex, fencePoint);
			m_GpuResourceAllocator->ReleaseTexture(texIndex, fencePoint);
		}
		m_MarkedReleaseTextureIndices.clear();

		for (const auto bufIndex : m_MarkedReleaseBufferIndices)
		{
			GGLAB_ASSERT_MSG(bufIndex.IsValid(), "Releasing an invalid buffer index.");
			m_GpuResourceAllocator->ReleaseBuffer(bufIndex, fencePoint);
		}
		m_MarkedReleaseBufferIndices.clear();

		m_ViewCache->GarbageCollect();
	}

	DX12Texture* RenderGraph::GetTexture(RGTextureId texId) noexcept
	{
		auto* virtualRes = static_cast<RGVirtualResource<RGTextureResource>*>(GetVirtualResource(texId));
		if (!virtualRes)
		{
			return nullptr;
		}

		if (virtualRes->m_Imported)
		{
			return virtualRes->m_ImportedNative;
		}

		if (!virtualRes->m_GpuResourceIndex.IsValid())
		{
			return nullptr;
		}
		return m_GpuResourceAllocator->GetTexture(virtualRes->m_GpuResourceIndex);
	}

	DX12Buffer* RenderGraph::GetBuffer(RGBufferId bufId) noexcept
	{
		auto* virtualRes = static_cast<RGVirtualResource<RGBufferResource>*>(GetVirtualResource(bufId));
		if (!virtualRes)
		{
			return nullptr;
		}

		if (virtualRes->m_Imported)
		{
			return virtualRes->m_ImportedNative;
		}

		if (!virtualRes->m_GpuResourceIndex.IsValid())
		{
			return nullptr;
		}
		return m_GpuResourceAllocator->GetBuffer(virtualRes->m_GpuResourceIndex);
	}

	ResourceIndex RenderGraph::GetResourceIndex(RGTextureId texId) noexcept
	{
		return GetResourceIndexImpl<RGTextureResource>(texId);
	}

	ResourceIndex RenderGraph::GetResourceIndex(RGBufferId bufId) noexcept
	{
		return GetResourceIndexImpl<RGBufferResource>(bufId);
	}

	RGVirtualResourceBase* RenderGraph::GetVirtualResource(RGResourceHandle handle) const noexcept
	{
		if (!handle.IsValid())
		{
			return nullptr;
		}

		const auto& slot = m_ResourceSlots[handle.GetHandle().Value()];
		const auto handleVersion = handle.GetVersion();
		if (handleVersion == RGResourceHandle::UnintializedVersion ||
			handleVersion > slot.m_Version)
		{
			return nullptr;
		}

		return m_VirtualResources[slot.m_VirtualResourceIndex.Value()];
	}

	RGResourceNode& RenderGraph::GetActiveResourceNode(RGResourceHandle handle) noexcept
	{
		auto& slot = m_ResourceSlots[handle.GetHandle().Value()];
		return m_ResourceNodes[slot.m_ResourceNodeIndex.Value()];
	}

	RGPassNode& RenderGraph::GetPassNode(RGPassNode::Index index) noexcept
	{
		return m_PassNodes[index.Value()];
	}
}
