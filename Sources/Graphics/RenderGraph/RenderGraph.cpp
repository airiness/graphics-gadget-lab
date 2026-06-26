#include "Core/Precompiled.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGPass.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/Cache/DX12ViewCache.h"

namespace gglab
{
	DX12DescriptorView RGExecuteContext::GetView(RGTextureViewId viewId) const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_RenderGraph);
		return m_RenderGraph ? m_RenderGraph->GetTextureView(viewId) : DX12DescriptorView{};
	}

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

		// Add pass to reachable and stack
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

		// Traverse passes and add it if it's a side effect pass
		for (uint32_t passNodeIndex = 0; passNodeIndex < m_PassNodes.size(); ++passNodeIndex)
		{
			auto& passNode = m_PassNodes[passNodeIndex];
			if (passNode.m_SideEffect)
			{
				addPass(RGPassNodeIndex{ passNodeIndex });
			}
		}

		// Traverse all passes in stack, find their reader resources, add their writer passes to stack and reachable
		while (!stack.empty())
		{
			const RGPassNodeIndex passNodeIndex = stack.back();
			stack.pop_back();

			RGPassNode& passNode = m_PassNodes[passNodeIndex.Value()];
			for (const auto& access : passNode.m_Accesses)
			{
				if (access.m_AccessType == RGResourceAccessType::Read)
				{
					RGResourceNode& resourceNode = m_ResourceNodes[access.m_ResourceNodeIndex.Value()];
					if (resourceNode.m_Writer.IsValid())
					{
						addPass(resourceNode.m_Writer);
					}
				}
			}
		}

		// Set and initialize pass infos for next steps
		for (uint32_t passNodeIndex = 0; passNodeIndex < m_PassNodes.size(); ++passNodeIndex)
		{
			auto& passNode = m_PassNodes[passNodeIndex];
			passNode.m_Culled = (reachable.find(RGPassNodeIndex{ passNodeIndex }) == reachable.end());

			passNode.m_PreBarriers.clear();
			passNode.m_PostBarriers.clear();
			passNode.m_DevirtualizeVirtualResources.clear();
			passNode.m_DestroyVirtualResources.clear();
		}

		// Set and initialize virtual resource infos for next steps
		for (auto& virtualResource : m_VirtualResources)
		{
			virtualResource->m_RefCount = 0;
			virtualResource->m_FirstUser = InvalidRGPassNodeIndex;
			virtualResource->m_LastUser = InvalidRGPassNodeIndex;
		}

		// Traverse all reachable passes, count reference for virtual resources and find first and last user pass for each virtual resource
		for (uint32_t passNodeIndex = 0; passNodeIndex < m_PassNodes.size(); ++passNodeIndex)
		{
			auto& passNode = m_PassNodes[passNodeIndex];
			if (passNode.m_Culled)
			{
				continue;
			}

			const RGPassNodeIndex rgPassNodeIndex{ passNodeIndex };
			for (const auto& access : passNode.m_Accesses)
			{
				RGResourceNode& resourceNode = m_ResourceNodes[access.m_ResourceNodeIndex.Value()];
				auto* virtualResource = resourceNode.m_VirtualResource;
				if (!virtualResource)
				{
					continue;
				}
				++virtualResource->m_RefCount;
				if (!virtualResource->m_FirstUser.IsValid())
				{
					virtualResource->m_FirstUser = rgPassNodeIndex;
				}
				virtualResource->m_LastUser = rgPassNodeIndex;
			}
		}

		// Traverse all virtual resources, add devirtualize intent to first user pass and destroy intent to last user pass
		for (auto* virtualResource : m_VirtualResources)
		{
			if (virtualResource->m_RefCount == 0)
			{
				continue;
			}

			m_PassNodes[virtualResource->m_FirstUser.Value()].m_DevirtualizeVirtualResources.push_back(virtualResource);
			m_PassNodes[virtualResource->m_LastUser.Value()].m_DestroyVirtualResources.push_back(virtualResource);
		}

		// Track resource barrier states while scanning passes in execution order.
		std::unordered_map<RGVirtualResourceBase*, RHIResourceState> currentStates;
		for (auto* virtualResource : m_VirtualResources)
		{
			currentStates.emplace(virtualResource, virtualResource->m_InitialBarrierState);
		}

		struct MergedAccess
		{
			RGVirtualResourceBase* m_VirtualResource = nullptr;
			uint64_t m_UsageBits = 0;
			RGResourceAccessType m_AccessType = RGResourceAccessType::Read;
		};

		for (auto& passNode : m_PassNodes)
		{
			if (passNode.m_Culled)
			{
				continue;
			}

			std::vector<MergedAccess> mergedAccesses;
			for (const auto& access : passNode.m_Accesses)
			{
				auto* virtualResource = m_ResourceNodes[access.m_ResourceNodeIndex.Value()].m_VirtualResource;
				if (!virtualResource)
				{
					continue;
				}

				auto iter = std::ranges::find(mergedAccesses, virtualResource, &MergedAccess::m_VirtualResource);
				if (iter == mergedAccesses.end())
				{
					mergedAccesses.push_back(
						{
							.m_VirtualResource = virtualResource,
							.m_UsageBits = access.m_UsageBits,
							.m_AccessType = access.m_AccessType,
						});
					continue;
				}

				GGLAB_ASSERT_MSG(iter->m_AccessType == access.m_AccessType,
					"A pass may not read and write the same whole resource.");
				iter->m_UsageBits |= access.m_UsageBits;
			}

			for (const auto& access : mergedAccesses)
			{
				const RHIResourceState requiredState = ToRHIResourceState(
					access.m_UsageBits,
					access.m_VirtualResource->m_ResourceType);
				auto& currentState = currentStates.at(access.m_VirtualResource);

				if (NeedsRHIResourceBarrier(currentState, requiredState))
				{
					passNode.m_PreBarriers.push_back(
						{
							.m_VirtualResource = access.m_VirtualResource,
							.m_Before = currentState,
							.m_After = requiredState,
						});
				}
				currentState = requiredState;
			}
		}

		for (auto* virtualResource : m_VirtualResources)
		{
			if (virtualResource->m_RefCount == 0)
			{
				continue;
			}

			const RHIResourceState requiredFinalState = virtualResource->m_Imported ?
				virtualResource->m_FinalBarrierState.value_or(virtualResource->m_InitialBarrierState) :
				CommonRHIResourceState();
			auto& currentState = currentStates.at(virtualResource);
			if (!NeedsRHIResourceBarrier(currentState, requiredFinalState))
			{
				continue;
			}

			m_PassNodes[virtualResource->m_LastUser.Value()].m_PostBarriers.push_back(
				{
					.m_VirtualResource = virtualResource,
					.m_Before = currentState,
					.m_After = requiredFinalState,
				});
			currentState = requiredFinalState;
		}
	}

	void RenderGraph::Execute(RGExecuteContext& executeContext) noexcept
	{
		auto* previousRenderGraph = executeContext.m_RenderGraph;
		executeContext.m_RenderGraph = this;

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

			EmitBarriers(executeContext.GetGraphicsCommandList(), passNode.m_PreBarriers);

			// Execute passes
			if (passNode.m_Pass)
			{
				passNode.m_Pass->Execute(executeContext);
			}

			EmitBarriers(executeContext.GetGraphicsCommandList(), passNode.m_PostBarriers);

			for (auto* virtualResource : passNode.m_DestroyVirtualResources)
			{
				virtualResource->Destroy(*this);
			}
		}

		executeContext.m_RenderGraph = previousRenderGraph;
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

	RHITextureHandle RenderGraph::GetTextureHandle(RGTextureId texId) noexcept
	{
		auto* virtualRes = static_cast<RGVirtualResource<RGTextureResource>*>(GetVirtualResource(texId));
		if (!virtualRes)
		{
			return {};
		}

		if (virtualRes->m_Imported)
		{
			return {};
		}

		if (!virtualRes->m_GpuResourceIndex.IsValid())
		{
			return {};
		}

		return m_GpuResourceAllocator->GetTextureHandle(virtualRes->m_GpuResourceIndex);
	}

	RHIBufferHandle RenderGraph::GetBufferHandle(RGBufferId bufId) noexcept
	{
		auto* virtualRes = static_cast<RGVirtualResource<RGBufferResource>*>(GetVirtualResource(bufId));
		if (!virtualRes)
		{
			return {};
		}

		if (virtualRes->m_Imported)
		{
			GGLAB_LOG_WARN("RenderGraph::GetBufferHandle() : Imported buffers do not expose RHI handles yet.");
			return {};
		}

		if (!virtualRes->m_GpuResourceIndex.IsValid())
		{
			return {};
		}

		return m_GpuResourceAllocator->GetBufferHandle(virtualRes->m_GpuResourceIndex);
	}

	RHITextureViewHandle RenderGraph::GetTextureViewHandle(RGTextureViewId viewId) noexcept
	{
		GGLAB_ASSERT_MSG(viewId.IsValid() && viewId.Value() < m_TextureViews.size(),
			"RenderGraph::GetTextureViewHandle received an invalid view id.");
		if (!viewId.IsValid() || viewId.Value() >= m_TextureViews.size())
		{
			return {};
		}

		const auto& view = m_TextureViews[viewId.Value()];
		const RHITextureHandle texture = GetTextureHandle(view.m_Texture);
		if (!texture.IsValid())
		{
			return {};
		}

		RHITextureViewDesc desc = view.m_Desc.value_or(RHITextureViewDesc{});
		desc.m_Type = view.m_Type;

		return m_ViewCache->GetOrCreateTextureView(texture, desc);
	}

	DX12DescriptorView RenderGraph::GetTextureView(RGTextureViewId viewId) noexcept
	{
		if (const RHITextureViewHandle viewHandle = GetTextureViewHandle(viewId);
			viewHandle.IsValid())
		{
			return m_ViewCache->ResolveTextureView(viewHandle);
		}

		GGLAB_ASSERT_MSG(viewId.IsValid() && viewId.Value() < m_TextureViews.size(),
			"RenderGraph::GetTextureView received an invalid view id.");
		if (!viewId.IsValid() || viewId.Value() >= m_TextureViews.size())
		{
			return {};
		}

		const auto& view = m_TextureViews[viewId.Value()];
		auto* texture = GetTexture(view.m_Texture);
		GGLAB_ASSERT_NOT_NULL(texture);
		if (!texture)
		{
			return {};
		}

		const auto resourceIndex = GetResourceIndex(view.m_Texture);
		GGLAB_ASSERT_MSG(resourceIndex.IsValid(),
			"RenderGraph texture view requires a valid resource index.");
		if (!resourceIndex.IsValid())
		{
			return {};
		}

		switch (view.m_Type)
		{
		case RHITextureViewType::RenderTarget:
		{
			const auto desc = view.m_Desc ?
				std::optional{ ToD3D12RenderTargetViewDesc(*view.m_Desc) } :
				std::nullopt;
			return m_ViewCache->GetOrCreate<ViewType::RTV>(
				resourceIndex,
				texture,
				desc);
		}
		case RHITextureViewType::DepthStencil:
		{
			const auto desc = view.m_Desc ?
				std::optional{ ToD3D12DepthStencilViewDesc(*view.m_Desc) } :
				std::nullopt;
			return m_ViewCache->GetOrCreate<ViewType::DSV>(
				resourceIndex,
				texture,
				desc);
		}
		case RHITextureViewType::ShaderResource:
		{
			const auto desc = view.m_Desc ?
				std::optional{ ToD3D12ShaderResourceViewDesc(*view.m_Desc) } :
				std::nullopt;
			return m_ViewCache->GetOrCreate<ViewType::SRV>(
				resourceIndex,
				texture,
				desc);
		}
		case RHITextureViewType::UnorderedAccess:
		{
			const auto desc = view.m_Desc ?
				std::optional{ ToD3D12UnorderedAccessViewDesc(*view.m_Desc) } :
				std::nullopt;
			return m_ViewCache->GetOrCreate<ViewType::UAV>(
				resourceIndex,
				texture,
				desc);
		}
		default:
			GGLAB_UNREACHABLE("RenderGraph texture view has an unknown view type.");
		}
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

	DX12Resource* RenderGraph::GetNativeResource(RGVirtualResourceBase* virtualResource) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(virtualResource);

		switch (virtualResource->m_ResourceType)
		{
		case RGResourceType::RGTexture:
		{
			auto* textureResource = static_cast<RGVirtualResource<RGTextureResource>*>(virtualResource);
			if (textureResource->m_Imported)
			{
				return textureResource->m_ImportedNative;
			}
			return m_GpuResourceAllocator->GetTexture(textureResource->m_GpuResourceIndex);
		}
		case RGResourceType::RGBuffer:
		{
			auto* bufferResource = static_cast<RGVirtualResource<RGBufferResource>*>(virtualResource);
			if (bufferResource->m_Imported)
			{
				return bufferResource->m_ImportedNative;
			}
			return m_GpuResourceAllocator->GetBuffer(bufferResource->m_GpuResourceIndex);
		}
		}

		GGLAB_UNREACHABLE("Unhandled RGResourceType.");
	}

	void RenderGraph::EmitBarriers(DX12CommandList* commandList,
		const std::vector<RGPassNode::BarrierIntent>& barriers) noexcept
	{
		if (barriers.empty())
		{
			return;
		}

		GGLAB_ASSERT_NOT_NULL(commandList);

		for (const auto& intent : barriers)
		{
			auto* resource = GetNativeResource(intent.m_VirtualResource);
			GGLAB_ASSERT_NOT_NULL(resource);
			AddDX12RGBarrier(
				*commandList,
				intent.m_VirtualResource->m_ResourceType,
				*resource,
				intent.m_Before,
				intent.m_After);
		}

		commandList->FlushBarriers();
	}
}
