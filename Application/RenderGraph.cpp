#include "Precompiled.h"
#include "RenderGraph.h"
#include "RGPass.h"
#include "DX12Device.h"
#include "DX12Texture.h"
#include "DX12Buffer.h"
#include "DX12SwapChain.h"

namespace gglab
{
	RenderGraph::RenderGraph(RGGpuResourceAllocator& gpuResAllocator) noexcept :
		m_GpuResourceAllocator(gpuResAllocator),
		m_ArenaAllocator(1u << 20)
	{
	}

	RenderGraph::~RenderGraph() noexcept
	{
	}

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
					RGResourceNode& resourceNode = m_ResourceNodes[access.m_ResourceNodeIndex];
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

		for (auto& virtualResource : m_VirtualResouces)
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
					RGResourceNode& resourceNode = m_ResourceNodes[index];
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

		for (auto* virtualResource : m_VirtualResouces)
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

	void RenderGraph::Retire(DX12FencePoint fencePoint) noexcept
	{
		for (const auto texIndex : m_MarkedReleaseTextureIndices)
		{
			m_GpuResourceAllocator.ReleaseTexture(texIndex, fencePoint);
		}
		m_MarkedReleaseTextureIndices.clear();

		for (const auto bufIndex : m_MarkedReleaseBufferIndices)
		{
			m_GpuResourceAllocator.ReleaseBuffer(bufIndex, fencePoint);
		}
		m_MarkedReleaseBufferIndices.clear();
	}

	RGVirtualResourceBase* RenderGraph::GetVirtualResource(RGResourceHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			return nullptr;
		}

		const auto& slot = m_ResourceSlots[handle.GetHandle()];
		if (slot.m_Version != handle.GetVersion())
		{
			return nullptr;
		}

		return m_VirtualResouces[slot.m_VirtualResourceIndex];
	}

	RGResourceNode& RenderGraph::GetActiveResourceNode(RGResourceHandle handle) noexcept
	{
		auto& slot = m_ResourceSlots[handle.GetHandle()];
		return m_ResourceNodes[slot.m_ResourceNodeIndex];
	}

	RGPassNode& RenderGraph::GetPassNode(RGPassNode::Index index) noexcept
	{
		return m_PassNodes[index];
	}
}