#include "Precompiled.h"
#include "RenderGraph.h"
#include "RGPass.h"
#include "DX12Device.h"
#include "DX12Texture.h"
#include "DX12Buffer.h"
#include "DX12SwapChain.h"

namespace graphicsGadgetLab
{
	RenderGraph::RenderGraph(DX12Device* dx12Device) noexcept :
		m_DX12Device(dx12Device),
		m_GpuResourceRegistry(dx12Device),
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

		for (auto& passNode : m_PassNodes)
		{
			if (passNode.m_SideEffect)
			{
				stack.push_back(&passNode);
			}
		}

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

	void RenderGraph::Execute() noexcept
	{
		auto* swapChain = m_DX12Device->GetSwapChain();
		const auto currentBufferIndex = swapChain->GetCurrentBackBufferIndex();
		auto* graphicsCommandList = m_DX12Device->GetGraphicsCommandList(currentBufferIndex);

		for (auto& passNode : m_PassNodes)
		{
			if(passNode.m_Culled)
			{
				continue;
			}

			// Devirtualize Gpu resources
			for (auto* virtualResource : passNode.m_DevirtualizeVirtualResources)
			{
				virtualResource->Devirtualize(m_GpuResourceRegistry);
			}

			// Execute passes
			if (passNode.m_Pass)
			{
				passNode.m_Pass->Execute(graphicsCommandList);
			}

			for (auto* virtualResource : passNode.m_DestroyVirtualResources)
			{
				virtualResource->Destroy(m_GpuResourceRegistry);
			}
		}
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
