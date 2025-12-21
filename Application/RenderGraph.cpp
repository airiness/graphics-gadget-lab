#include "Precompiled.h"
#include "RenderGraph.h"
#include "RGPass.h"
#include "DX12Device.h"
#include "DX12Texture.h"
#include "DX12Buffer.h"
#include "DX12ViewCache.h"

namespace gglab
{
	RenderGraph::RenderGraph(const CreateInfo& createInfo) noexcept :
		m_GpuResourceAllocator(createInfo.m_GpuResourceAllocator),
		m_ViewCache(createInfo.m_ViewCache),
		m_ArenaAllocator(1u << 20),
		m_Blackboard(m_ArenaAllocator)
	{
		GGLAB_ASSERT_MSG(m_GpuResourceAllocator != nullptr, "GpuResourceAllocator can not be null.");
		GGLAB_ASSERT_MSG(m_ViewCache != nullptr, "DX12ViewCache can not be null.");
	}

	RenderGraph::~RenderGraph() noexcept = default;

	void RenderGraph::Compile() noexcept
	{
		std::vector<RGPassNode*> stack;
		std::unordered_set<RGPassNode*> reachable;

		auto addPass = [&stack, &reachable](RGPassNode* passNode)
			{
				if (!passNode)
				{
					return;
				}
				if (reachable.insert(passNode).second)
				{
					stack.push_back(passNode);
				}
			};

		for (auto& passNode : m_PassNodes)
		{
			if (passNode.m_SideEffect)
			{
				addPass(&passNode);
			}
		}

		while (!stack.empty())
		{
			RGPassNode* passNode = stack.back();
			stack.pop_back();

			for (const auto& access : passNode->m_Accesses)
			{
				if (access.m_AccessType == RGPassNode::Access::Type::Read)
				{
					RGResourceNode& resourceNode = m_ResourceNodes[access.m_ResourceNodeIndex.Value()];
					if (resourceNode.m_Writer)
					{
						addPass(resourceNode.m_Writer);
					}
				}
			}
		}

		for (auto& passNode : m_PassNodes)
		{
			passNode.m_Culled = (reachable.find(&passNode) == reachable.end());

			passNode.m_DevirtualizeVirtualResources.clear();
			passNode.m_DestroyVirtualResources.clear();
		}

		for (auto& virtualResource : m_VirtualResources)
		{
			virtualResource->m_RefCount = 0;
			virtualResource->m_FirstUser = nullptr;
			virtualResource->m_LastUser = nullptr;
		}

		for (auto& passNode : m_PassNodes)
		{
			if (passNode.m_Culled)
			{
				continue;
			}

			auto markUse = [this, &passNode](RGResourceNode::Index index)
				{
					RGResourceNode& resourceNode = m_ResourceNodes[index.Value()];
					auto* virtualResource = resourceNode.m_VirtualResource;
					if (!virtualResource)
					{
						return;
					}
					++virtualResource->m_RefCount;
					if (!virtualResource->m_FirstUser)
					{
						virtualResource->m_FirstUser = &passNode;
					}
					virtualResource->m_LastUser = &passNode;

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

			virtualResource->m_FirstUser->m_DevirtualizeVirtualResources.push_back(virtualResource);
			virtualResource->m_LastUser->m_DestroyVirtualResources.push_back(virtualResource);
		}
	}

	void RenderGraph::Execute(RGExecuteContext& executeContext) noexcept
	{
		auto* graphicsCommandList = executeContext.m_GraphicsCommandList;

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
				passNode.m_Pass->Execute(graphicsCommandList);
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
		if (slot.m_Version != handle.GetVersion())
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

	ResourceIndex RenderGraph::GetOrCreateExternalIndex(const DX12Texture* texture) noexcept
	{
		return GetOrCreateExternalIndexImpl(texture,
			m_ExternalTextureIndices,
			m_NextExternalTextureId,
			ExternalResourceIndex::Type::Texture);
	}

	ResourceIndex RenderGraph::GetOrCreateExternalIndex(const DX12Buffer* buffer) noexcept
	{
		return GetOrCreateExternalIndexImpl(buffer,
			m_ExternalBufferIndices,
			m_NextExternalBufferId,
			ExternalResourceIndex::Type::Buffer);
	}

	void RenderGraph::ForgetExternal(DX12Texture* texture, bool freeViewsImmediately) noexcept
	{
		if (!texture)
		{
			return;
		}

		auto iter = m_ExternalTextureIndices.find(texture);
		if (iter == m_ExternalTextureIndices.end())
		{
			return;
		}

		const ResourceIndex index = iter->second;
		if (freeViewsImmediately && m_ViewCache)
		{
			m_ViewCache->FreeAllImmediately(index);
		}

		m_ExternalTextureIndices.erase(iter);
	}

	void RenderGraph::ForgetExternal(DX12Buffer* buffer) noexcept
	{
		if (!buffer)
		{
			return;
		}

		m_ExternalBufferIndices.erase(buffer);
	}
}