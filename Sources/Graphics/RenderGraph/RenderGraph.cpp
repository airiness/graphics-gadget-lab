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

		BuildDependencyGraph();
		CullPasses();
		TopologicalSortPasses();
		BuildResourceLifetimes();
		BuildResourceBarriers();
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
			RGResourceNode::Index resourceNodeIndex) noexcept
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
					return;
				}

				m_DependencyEdges.push_back(
					{
						.m_From = from,
						.m_To = to,
						.m_ResourceNodeIndex = resourceNodeIndex,
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
				addEdge(resourceNode.m_Writer, readerPassIndex, stableResourceNodeIndex);
			}

			if (!resourceNode.m_Previous.IsValid() || !resourceNode.m_Writer.IsValid())
			{
				continue;
			}

			const RGResourceNode& previousNode = m_ResourceNodes[resourceNode.m_Previous.Value()];
			addEdge(previousNode.m_Writer, resourceNode.m_Writer, resourceNode.m_Previous);
			for (const auto previousReader : previousNode.m_Readers)
			{
				addEdge(previousReader, resourceNode.m_Writer, resourceNode.m_Previous);
			}
		}
	}

	void RenderGraph::CullPasses() noexcept
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
			if (m_PassNodes[passNodeIndex].m_SideEffect)
			{
				addPass(RGPassNodeIndex{ passNodeIndex });
			}
		}

		while (!stack.empty())
		{
			const RGPassNodeIndex passNodeIndex = stack.back();
			stack.pop_back();

			for (const auto dependency : m_PassNodes[passNodeIndex.Value()].m_Dependencies)
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

	void RenderGraph::TopologicalSortPasses() noexcept
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
			return;
		}

		m_ExecutionOrder.clear();
		for (uint32_t passNodeIndex = 0; passNodeIndex < m_PassNodes.size(); ++passNodeIndex)
		{
			if (!m_PassNodes[passNodeIndex].m_Culled)
			{
				m_PassNodes[passNodeIndex].m_ExecutionOrder =
					static_cast<int32_t>(m_ExecutionOrder.size());
				m_ExecutionOrder.push_back(RGPassNodeIndex{ passNodeIndex });
			}
		}
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
		struct TextureStateTracker
		{
			RHIResourceState m_InitialState = CommonRHIResourceState();
			std::unordered_map<uint32_t, RHIResourceState> m_SubresourceStates;
		};

		auto normalizeTextureRange = [](const RHITextureDesc& desc,
			const std::optional<RHISubresourceRange>& requested) noexcept
			{
				auto resolveCount = [](uint32_t base, uint32_t count, uint32_t total) noexcept
					{
						if (base >= total)
						{
							return 0u;
						}

						const uint32_t remaining = total - base;
						return count == RHISubresourceRange::Remaining ?
							remaining :
							std::min(count, remaining);
					};

				RHISubresourceRange range = requested.value_or(
					RHISubresourceRange
					{
						.m_PlaneCount = RHISubresourceRange::Remaining,
					});

				const uint32_t mipLevels = std::max<uint32_t>(1, desc.m_MipLevels);
				const uint32_t arraySize = desc.m_Dimension == RHITextureDimension::Texture3D ?
					1u :
					std::max<uint32_t>(1, desc.m_ArraySize);
				const uint32_t planeCount = GetRHIFormatPlaneCount(desc.m_Format);

				range.m_MipCount = resolveCount(range.m_BaseMip, range.m_MipCount, mipLevels);
				range.m_ArraySliceCount = resolveCount(
					range.m_BaseArraySlice,
					range.m_ArraySliceCount,
					arraySize);
				range.m_PlaneCount = resolveCount(range.m_BasePlane, range.m_PlaneCount, planeCount);
				return range;
			};

		auto subresourceIndex = [](const RHITextureDesc& desc,
			uint32_t mip,
			uint32_t arraySlice,
			uint32_t plane) noexcept
			{
				const uint32_t mipLevels = std::max<uint32_t>(1, desc.m_MipLevels);
				const uint32_t arraySize = std::max<uint32_t>(1, desc.m_ArraySize);
				return mip + mipLevels * (arraySlice + arraySize * plane);
			};

		auto singleSubresourceRange = [](uint32_t mip, uint32_t arraySlice, uint32_t plane) noexcept
			{
				return RHISubresourceRange
				{
					.m_BaseMip = mip,
					.m_MipCount = 1,
					.m_BaseArraySlice = arraySlice,
					.m_ArraySliceCount = 1,
					.m_BasePlane = plane,
					.m_PlaneCount = 1,
				};
			};

		std::unordered_map<RGVirtualResourceBase*, TextureStateTracker> textureStates;
		std::unordered_map<RGVirtualResourceBase*, RHIResourceState> bufferStates;
		for (auto* virtualResource : m_VirtualResources)
		{
			if (virtualResource->m_ResourceType == RGResourceType::RGTexture)
			{
				textureStates.emplace(virtualResource,
					TextureStateTracker{ .m_InitialState = virtualResource->m_InitialBarrierState });
			}
			else
			{
				bufferStates.emplace(virtualResource, virtualResource->m_InitialBarrierState);
			}
		}

		for (const auto passNodeIndex : m_ExecutionOrder)
		{
			auto& passNode = m_PassNodes[passNodeIndex.Value()];
			for (const auto& access : passNode.m_Accesses)
			{
				const RHIResourceState requiredState = ToRHIResourceState(
					access.m_AccessValue,
					access.m_ResourceType);

				auto* virtualResource = m_ResourceNodes[access.m_ResourceNodeIndex.Value()].m_VirtualResource;
				if (!virtualResource)
				{
					continue;
				}

				if (virtualResource->m_ResourceType == RGResourceType::RGTexture)
				{
					const auto* textureResource =
						static_cast<const RGVirtualResource<RGTextureResource>*>(virtualResource);
					const RHISubresourceRange range = normalizeTextureRange(
						textureResource->m_Desc,
						access.m_Subresources);
					auto& stateTracker = textureStates.at(virtualResource);

					for (uint32_t plane = range.m_BasePlane; plane < range.m_BasePlane + range.m_PlaneCount; ++plane)
					{
						for (uint32_t arraySlice = range.m_BaseArraySlice;
							arraySlice < range.m_BaseArraySlice + range.m_ArraySliceCount;
							++arraySlice)
						{
							for (uint32_t mip = range.m_BaseMip; mip < range.m_BaseMip + range.m_MipCount; ++mip)
							{
								const uint32_t index = subresourceIndex(textureResource->m_Desc, mip, arraySlice, plane);
								auto [stateIter, inserted] = stateTracker.m_SubresourceStates.emplace(
									index,
									stateTracker.m_InitialState);
								auto& currentState = stateIter->second;

								if (NeedsRHIResourceBarrier(currentState, requiredState))
								{
									passNode.m_PreBarriers.push_back(
										{
											.m_VirtualResource = virtualResource,
											.m_Before = currentState,
											.m_After = requiredState,
											.m_Subresources = singleSubresourceRange(mip, arraySlice, plane),
										});
								}
								currentState = requiredState;
							}
						}
					}
					continue;
				}

				auto& currentState = bufferStates.at(virtualResource);
				if (NeedsRHIResourceBarrier(currentState, requiredState))
				{
					passNode.m_PreBarriers.push_back(
						{
							.m_VirtualResource = virtualResource,
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

			if (virtualResource->m_ResourceType == RGResourceType::RGTexture)
			{
				auto& stateTracker = textureStates.at(virtualResource);
				for (auto& [subresourceIndex, currentState] : stateTracker.m_SubresourceStates)
				{
					if (!NeedsRHIResourceBarrier(currentState, requiredFinalState))
					{
						continue;
					}

					const auto* textureResource =
						static_cast<const RGVirtualResource<RGTextureResource>*>(virtualResource);
					const uint32_t mipLevels = std::max<uint32_t>(1, textureResource->m_Desc.m_MipLevels);
					const uint32_t arraySize = std::max<uint32_t>(1, textureResource->m_Desc.m_ArraySize);
					const uint32_t mip = subresourceIndex % mipLevels;
					const uint32_t arraySlice = (subresourceIndex / mipLevels) % arraySize;
					const uint32_t plane = subresourceIndex / (mipLevels * arraySize);

					m_PassNodes[virtualResource->m_LastUser.Value()].m_PostBarriers.push_back(
						{
							.m_VirtualResource = virtualResource,
							.m_Before = currentState,
							.m_After = requiredFinalState,
							.m_Subresources = singleSubresourceRange(mip, arraySlice, plane),
						});
					currentState = requiredFinalState;
				}
				continue;
			}

			auto& currentState = bufferStates.at(virtualResource);
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
