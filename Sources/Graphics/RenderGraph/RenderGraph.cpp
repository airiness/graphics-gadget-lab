#include "Core/Precompiled.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGPass.h"
#include "Graphics/RenderGraph/RGBarrierCompiler.h"

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

	bool RenderGraph::Compile() noexcept
	{
		m_Compiled = false;
		for (uint32_t passNodeIndex = 0; passNodeIndex < m_PassNodes.size(); ++passNodeIndex)
		{
			auto& passNode = m_PassNodes[passNodeIndex];
			passNode.m_Culled = false;
			passNode.m_ExecutionOrder = -1;
			passNode.m_PreBarriers.clear();
			passNode.m_PostBarriers.clear();
			passNode.m_DevirtualizeVirtualResources.clear();
			passNode.m_DestroyVirtualResources.clear();
		}
		if (!m_BuildValid)
		{
			GGLAB_LOG_GRAPHICS_ERROR("RenderGraph compilation rejected invalid resource declarations.");
			return false;
		}

		BuildDependencyGraph();
		CullPasses();
		if (!TopologicalSortPasses())
		{
			return false;
		}
		BuildResourceLifetimes();
		BuildResourceBarriers();
		m_Compiled = true;
		return true;
	}

	void RenderGraph::BuildDependencyGraph() noexcept
	{
		m_DependencyEdges.clear();
		for (auto& passNode : m_PassNodes)
		{
			passNode.m_Dependencies.clear();
			passNode.m_Dependents.clear();
		}

		auto addEdge = [this](RGPassNodeIndex from,
			RGPassNodeIndex to,
			RGResourceNode::Index resourceNodeIndex,
			RGDependencyReason reason) noexcept
			{
				if (!from.IsValid() || !to.IsValid() || from == to)
				{
					return;
				}

				auto edgeIter = std::ranges::find_if(m_DependencyEdges,
					[&](const RGPassDependencyEdge& edge)
					{
						return edge.m_From == from &&
							edge.m_To == to &&
							edge.m_ResourceNodeIndex == resourceNodeIndex;
					});
				if (edgeIter != m_DependencyEdges.end())
				{
					// A pair may first be discovered as a hazard and later as a true
					// data dependency. Preserve the stronger liveness semantics.
					if (!IsRGLivenessDependency(edgeIter->m_Reason) &&
						IsRGLivenessDependency(reason))
					{
						edgeIter->m_Reason = reason;
					}
					return;
				}

				m_DependencyEdges.push_back(
					{
						.m_From = from,
						.m_To = to,
						.m_ResourceNodeIndex = resourceNodeIndex,
						.m_Reason = reason,
					});
				m_PassNodes[from.Value()].m_Dependents.push_back(to);
				m_PassNodes[to.Value()].m_Dependencies.push_back(from);
			};

		for (uint32_t resourceNodeIndex = 0; resourceNodeIndex < m_ResourceNodes.size(); ++resourceNodeIndex)
		{
			const RGResourceNode& resourceNode = m_ResourceNodes[resourceNodeIndex];
			const RGResourceNode::Index stableResourceNodeIndex{ resourceNodeIndex };

			for (const auto readerPassIndex : resourceNode.m_Readers)
			{
				addEdge(
					resourceNode.m_Writer,
					readerPassIndex,
					stableResourceNodeIndex,
					RGDependencyReason::WriterToReader);
			}

			if (!resourceNode.m_Previous.IsValid() || !resourceNode.m_Writer.IsValid())
			{
				continue;
			}

			const RGResourceNode& previousNode = m_ResourceNodes[resourceNode.m_Previous.Value()];
			addEdge(
				previousNode.m_Writer,
				resourceNode.m_Writer,
				resourceNode.m_Previous,
				RGDependencyReason::PreviousWriterToWriter);
			for (const auto previousReader : previousNode.m_Readers)
			{
				addEdge(
					previousReader,
					resourceNode.m_Writer,
					resourceNode.m_Previous,
					RGDependencyReason::PreviousReaderToWriter);
			}
		}

		for (const auto* virtualResource : m_VirtualResources)
		{
			if (!virtualResource->m_ExportPass.IsValid())
			{
				continue;
			}

			GGLAB_ASSERT_MSG(
				virtualResource->m_ExportResourceNodeIndex < m_ResourceNodes.size(),
				"RenderGraph export references an invalid resource node.");
			const RGResourceNode::Index resourceNodeIndex{ virtualResource->m_ExportResourceNodeIndex };
			const auto& resourceNode = m_ResourceNodes[resourceNodeIndex.Value()];
			addEdge(
				resourceNode.m_Writer,
				virtualResource->m_ExportPass,
				resourceNodeIndex,
				RGDependencyReason::ExportWriterToExport);
			for (const auto readerPassIndex : resourceNode.m_Readers)
			{
				addEdge(
					readerPassIndex,
					virtualResource->m_ExportPass,
					resourceNodeIndex,
					RGDependencyReason::ExportReaderToExport);
			}
		}
	}

	void RenderGraph::CullPasses() noexcept
	{
		std::vector<RGPassNodeIndex> stack;
		std::unordered_set<RGPassNodeIndex> reachable;
		// Hazard edges remain in RGPassNode's full adjacency for topological sorting,
		// but cannot make an otherwise dead producer reachable.
		std::vector<std::vector<RGPassNodeIndex>> livenessDependencies(m_PassNodes.size());
		for (const auto& edge : m_DependencyEdges)
		{
			if (IsRGLivenessDependency(edge.m_Reason))
			{
				livenessDependencies[edge.m_To.Value()].push_back(edge.m_From);
			}
		}

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
			if (m_PassNodes[passNodeIndex].m_SideEffect)
			{
				addPass(RGPassNodeIndex{ passNodeIndex });
			}
		}

		while (!stack.empty())
		{
			const RGPassNodeIndex passNodeIndex = stack.back();
			stack.pop_back();

			for (const auto dependency : livenessDependencies[passNodeIndex.Value()])
			{
				addPass(dependency);
			}
		}

		for (uint32_t passNodeIndex = 0; passNodeIndex < m_PassNodes.size(); ++passNodeIndex)
		{
			m_PassNodes[passNodeIndex].m_Culled =
				(reachable.find(RGPassNodeIndex{ passNodeIndex }) == reachable.end());
		}
	}

	bool RenderGraph::TopologicalSortPasses() noexcept
	{
		m_ExecutionOrder.clear();
		std::vector<uint32_t> indegrees(m_PassNodes.size(), 0);
		std::vector<RGPassNodeIndex> ready;

		for (uint32_t passNodeIndex = 0; passNodeIndex < m_PassNodes.size(); ++passNodeIndex)
		{
			const auto& passNode = m_PassNodes[passNodeIndex];
			if (passNode.m_Culled)
			{
				continue;
			}

			for (const auto dependency : passNode.m_Dependencies)
			{
				if (!m_PassNodes[dependency.Value()].m_Culled)
				{
					++indegrees[passNodeIndex];
				}
			}

			if (indegrees[passNodeIndex] == 0)
			{
				ready.push_back(RGPassNodeIndex{ passNodeIndex });
			}
		}

		while (!ready.empty())
		{
			std::ranges::sort(ready);
			const RGPassNodeIndex passNodeIndex = ready.front();
			ready.erase(ready.begin());

			m_PassNodes[passNodeIndex.Value()].m_ExecutionOrder =
				static_cast<int32_t>(m_ExecutionOrder.size());
			m_ExecutionOrder.push_back(passNodeIndex);

			for (const auto dependent : m_PassNodes[passNodeIndex.Value()].m_Dependents)
			{
				if (m_PassNodes[dependent.Value()].m_Culled)
				{
					continue;
				}

				GGLAB_ASSERT_MSG(indegrees[dependent.Value()] > 0, "RenderGraph topological sort indegree underflow.");
				--indegrees[dependent.Value()];
				if (indegrees[dependent.Value()] == 0)
				{
					ready.push_back(dependent);
				}
			}
		}

		uint32_t reachablePassCount = 0;
		for (const auto& passNode : m_PassNodes)
		{
			reachablePassCount += passNode.m_Culled ? 0u : 1u;
		}

		GGLAB_ASSERT_MSG(m_ExecutionOrder.size() == reachablePassCount,
			"RenderGraph dependency graph contains a cycle.");
		if (m_ExecutionOrder.size() == reachablePassCount)
		{
			return true;
		}

		GGLAB_LOG_GRAPHICS_ERROR("RenderGraph dependency graph contains a cycle; execution is disabled.");
		m_ExecutionOrder.clear();
		for (auto& passNode : m_PassNodes)
		{
			passNode.m_ExecutionOrder = -1;
		}
		return false;
	}

	void RenderGraph::BuildResourceLifetimes() noexcept
	{
		for (auto& virtualResource : m_VirtualResources)
		{
			virtualResource->m_RefCount = 0;
			virtualResource->m_FirstUser = InvalidRGPassNodeIndex;
			virtualResource->m_LastUser = InvalidRGPassNodeIndex;
			virtualResource->ResetCompiledUsage();
		}

		for (const auto passNodeIndex : m_ExecutionOrder)
		{
			auto& passNode = m_PassNodes[passNodeIndex.Value()];
			for (const auto& access : passNode.m_Accesses)
			{
				auto* virtualResource = m_ResourceNodes[access.m_ResourceNodeIndex.Value()].m_VirtualResource;
				if (!virtualResource)
				{
					continue;
				}

				++virtualResource->m_RefCount;
				virtualResource->AccumulateAccess(access.m_AccessValue);
				if (!virtualResource->m_FirstUser.IsValid())
				{
					virtualResource->m_FirstUser = passNodeIndex;
				}
				virtualResource->m_LastUser = passNodeIndex;
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

	void RenderGraph::BuildResourceBarriers() noexcept
	{
		RGBarrierCompiler(
			m_VirtualResources,
			m_ResourceNodes,
			m_PassNodes,
			m_ExecutionOrder).Build();
	}

	void RenderGraph::Execute(RGExecuteContext& executeContext) noexcept
	{
		GGLAB_ASSERT_MSG(m_Compiled, "RenderGraph::Execute requires a successful Compile().");
		if (!m_Compiled)
		{
			GGLAB_LOG_GRAPHICS_ERROR("RenderGraph execution skipped because compilation failed.");
			return;
		}

		auto* previousRenderGraph = executeContext.m_RenderGraph;
		executeContext.m_RenderGraph = this;

		for (const auto passNodeIndex : m_ExecutionOrder)
		{
			auto& passNode = m_PassNodes[passNodeIndex.Value()];

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
					textureBarriers.push_back(
						{
							.m_Texture = handle,
							.m_Before = intent.m_Before,
							.m_After = intent.m_After,
							.m_Subresources = intent.m_Subresources,
						});
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
