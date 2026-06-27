#include "Core/Precompiled.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGPass.h"

namespace gglab
{
	RHITextureViewHandle RGExecuteContext::GetViewHandle(RGTextureViewId viewId) const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_RenderGraph);
		return m_RenderGraph ? m_RenderGraph->GetTextureViewHandle(viewId) : RHITextureViewHandle{};
	}

	RHIDescriptorHandle RGExecuteContext::GetViewDescriptor(RGTextureViewId viewId) const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_RenderGraph);
		if (!m_RenderGraph)
		{
			return {};
		}
		return m_RenderGraph->GetTextureViewDescriptor(viewId);
	}

	RenderGraph::RenderGraph(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_TransientResourcePool(createInfo.m_TransientResourcePool),
		m_ArenaAllocator(1u << 20),
		m_Blackboard(m_ArenaAllocator)
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "RHIDevice can not be null.");
		GGLAB_ASSERT_MSG(m_TransientResourcePool != nullptr, "TransientResourcePool can not be null.");
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
				if (access.m_DependencyAccess != RGDependencyAccess::Write)
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
			virtualResource->ResetCompiledUsage();
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
				virtualResource->AccumulateAccess(access.m_AccessValue);
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
			uint64_t m_AccessValue = 0;
			RGDependencyAccess m_DependencyAccess = RGDependencyAccess::Read;
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
							.m_AccessValue = access.m_AccessValue,
							.m_DependencyAccess = access.m_DependencyAccess,
						});
					continue;
				}

				GGLAB_ASSERT_MSG(iter->m_DependencyAccess == access.m_DependencyAccess,
					"A pass may not read and write the same whole resource.");
				GGLAB_ASSERT_MSG(iter->m_AccessValue == access.m_AccessValue,
					"A pass must declare a single access mode for each whole resource.");
			}

			for (const auto& access : mergedAccesses)
			{
				const RHIResourceState requiredState = ToRHIResourceState(
					access.m_AccessValue,
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
				virtualResource->Devirtualize(m_TransientResourcePool);
			}

			TrackPassResourceUses(executeContext.GetGraphicsCommandContext(), passNode);
			EmitBarriers(executeContext.GetGraphicsCommandContext(), passNode.m_PreBarriers);

			// Execute passes
			if (passNode.m_Pass)
			{
				passNode.m_Pass->Execute(executeContext);
			}

			EmitBarriers(executeContext.GetGraphicsCommandContext(), passNode.m_PostBarriers);

			for (auto* virtualResource : passNode.m_DestroyVirtualResources)
			{
				virtualResource->Destroy(*this);
			}
		}

		executeContext.m_RenderGraph = previousRenderGraph;
	}

	void RenderGraph::Retire(const RHIFencePoint& fencePoint) noexcept
	{
		for (auto& allocation : m_MarkedRetireTextures)
		{
			GGLAB_ASSERT_MSG(allocation.IsValid(), "Retiring an invalid texture allocation.");
			m_TransientResourcePool->RetireTexture(std::move(allocation), fencePoint);
		}
		m_MarkedRetireTextures.clear();

		for (auto& allocation : m_MarkedRetireBuffers)
		{
			GGLAB_ASSERT_MSG(allocation.IsValid(), "Retiring an invalid buffer allocation.");
			m_TransientResourcePool->RetireBuffer(std::move(allocation), fencePoint);
		}
		m_MarkedRetireBuffers.clear();

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
			return m_Device->IsAlive(virtualRes->m_ImportedHandle) ?
				virtualRes->m_ImportedHandle : RHITextureHandle{};
		}

		if (!virtualRes->m_PhysicalAllocation.IsValid())
		{
			return {};
		}

		return m_Device->IsAlive(virtualRes->m_PhysicalAllocation.m_Texture) ?
			virtualRes->m_PhysicalAllocation.m_Texture : RHITextureHandle{};
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
			return m_Device->IsAlive(virtualRes->m_ImportedHandle) ?
				virtualRes->m_ImportedHandle : RHIBufferHandle{};
		}

		if (!virtualRes->m_PhysicalAllocation.IsValid())
		{
			return {};
		}

		return m_Device->IsAlive(virtualRes->m_PhysicalAllocation.m_Buffer) ?
			virtualRes->m_PhysicalAllocation.m_Buffer : RHIBufferHandle{};
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

		return m_Device->CreateTextureView(texture, desc);
	}

	RHIDescriptorHandle RenderGraph::GetTextureViewDescriptor(RGTextureViewId viewId) noexcept
	{
		const RHITextureViewHandle view = GetTextureViewHandle(viewId);
		return view.IsValid() ? m_Device->GetTextureViewDescriptor(view) : RHIDescriptorHandle{};
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

	void RenderGraph::TrackPassResourceUses(RHICommandContext* commandContext, const RGPassNode& passNode) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(commandContext);
		if (!commandContext)
		{
			return;
		}

		for (const auto& access : passNode.m_Accesses)
		{
			auto* virtualResource = m_ResourceNodes[access.m_ResourceNodeIndex.Value()].m_VirtualResource;
			GGLAB_ASSERT_NOT_NULL(virtualResource);
			if (!virtualResource)
			{
				continue;
			}

			switch (virtualResource->m_ResourceType)
			{
			case RGResourceType::RGTexture:
			{
				auto* resource = static_cast<RGVirtualResource<RGTextureResource>*>(virtualResource);
				const RHITextureHandle handle = resource->m_Imported ?
					resource->m_ImportedHandle :
					resource->m_PhysicalAllocation.m_Texture;
				GGLAB_ASSERT_MSG(handle.IsValid(), "RenderGraph texture access requires a live RHI handle.");
				if (handle.IsValid())
				{
					commandContext->TrackTextureUse(handle);
				}
				break;
			}
			case RGResourceType::RGBuffer:
			{
				auto* resource = static_cast<RGVirtualResource<RGBufferResource>*>(virtualResource);
				const RHIBufferHandle handle = resource->m_Imported ?
					resource->m_ImportedHandle :
					resource->m_PhysicalAllocation.m_Buffer;
				GGLAB_ASSERT_MSG(handle.IsValid(), "RenderGraph buffer access requires a live RHI handle.");
				if (handle.IsValid())
				{
					commandContext->TrackBufferUse(handle);
				}
				break;
			}
			default:
				GGLAB_UNREACHABLE("Unhandled RGResourceType.");
			}
		}
	}

	void RenderGraph::EmitBarriers(RHICommandContext* commandContext,
		const std::vector<RGPassNode::BarrierIntent>& barriers) noexcept
	{
		if (barriers.empty())
		{
			return;
		}

		GGLAB_ASSERT_NOT_NULL(commandContext);
		if (!commandContext)
		{
			return;
		}

		std::vector<RHITextureBarrier> textureBarriers;
		std::vector<RHIBufferBarrier> bufferBarriers;
		textureBarriers.reserve(barriers.size());
		bufferBarriers.reserve(barriers.size());

		for (const auto& intent : barriers)
		{
			GGLAB_ASSERT_NOT_NULL(intent.m_VirtualResource);
			switch (intent.m_VirtualResource->m_ResourceType)
			{
			case RGResourceType::RGTexture:
			{
				auto* resource = static_cast<RGVirtualResource<RGTextureResource>*>(intent.m_VirtualResource);
				const RHITextureHandle handle = resource->m_Imported ?
					resource->m_ImportedHandle :
					resource->m_PhysicalAllocation.m_Texture;
				GGLAB_ASSERT_MSG(handle.IsValid(), "RenderGraph texture barrier requires a live RHI handle.");
				if (handle.IsValid())
				{
					textureBarriers.push_back({ handle, intent.m_Before, intent.m_After });
				}
				break;
			}
			case RGResourceType::RGBuffer:
			{
				auto* resource = static_cast<RGVirtualResource<RGBufferResource>*>(intent.m_VirtualResource);
				const RHIBufferHandle handle = resource->m_Imported ?
					resource->m_ImportedHandle :
					resource->m_PhysicalAllocation.m_Buffer;
				GGLAB_ASSERT_MSG(handle.IsValid(), "RenderGraph buffer barrier requires a live RHI handle.");
				if (handle.IsValid())
				{
					bufferBarriers.push_back({ handle, intent.m_Before, intent.m_After });
				}
				break;
			}
			default:
				GGLAB_UNREACHABLE("Unhandled RGResourceType.");
			}
		}

		commandContext->TextureBarrier(textureBarriers);
		commandContext->BufferBarrier(bufferBarriers);
	}
}
