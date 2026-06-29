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
			addEdge(resourceNode.m_Writer, virtualResource->m_ExportPass, resourceNodeIndex);
			for (const auto readerPassIndex : resourceNode.m_Readers)
			{
				addEdge(readerPassIndex, virtualResource->m_ExportPass, resourceNodeIndex);
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
		struct TextureStateTracker
		{
			RHIResourceState m_InitialState = CommonRHIResourceState();
			std::unordered_map<uint32_t, RHIResourceState> m_SubresourceStates;
		};
		struct TextureSubresource
		{
			uint32_t m_Mip = 0;
			uint32_t m_ArraySlice = 0;
			RHITextureAspect m_Aspect = RHITextureAspect::Color;
		};
		struct TextureTransition
		{
			TextureSubresource m_Subresource;
			RHIResourceState m_Before;
			RHIResourceState m_After;
		};

		constexpr std::array aspectOrder =
		{
			RHITextureAspect::Color,
			RHITextureAspect::Depth,
			RHITextureAspect::Stencil,
		};

		auto mipLevelsOf = [](const RHITextureDesc& desc) noexcept
			{
				return std::max<uint32_t>(1, desc.m_MipLevels);
			};
		auto arraySizeOf = [](const RHITextureDesc& desc) noexcept
			{
				return desc.m_Dimension == RHITextureDimension::Texture3D ?
					1u :
					std::max<uint32_t>(1, desc.m_ArraySize);
			};
		auto planeCountOf = [](const RHITextureDesc& desc) noexcept
			{
				return GetRHITexturePlaneCount(desc);
			};
		auto aspectIndexOf = [&](const RHITextureDesc& desc, RHITextureAspect target) noexcept
			{
				uint32_t index = 0;
				const RHITextureAspect aspects = GetRHITextureAspects(desc);
				for (const auto aspect : aspectOrder)
				{
					if (!Test(aspects, aspect))
					{
						continue;
					}
					if (aspect == target)
					{
						return index;
					}
					++index;
				}
				GGLAB_UNREACHABLE("Texture aspect is not supported by the resource format.");
			};
		auto aspectFromIndex = [&](const RHITextureDesc& desc, uint32_t targetIndex) noexcept
			{
				uint32_t index = 0;
				const RHITextureAspect aspects = GetRHITextureAspects(desc);
				for (const auto aspect : aspectOrder)
				{
					if (!Test(aspects, aspect))
					{
						continue;
					}
					if (index == targetIndex)
					{
						return aspect;
					}
					++index;
				}
				GGLAB_UNREACHABLE("Texture aspect index is out of range.");
			};

		auto normalizeTextureRange = [&](const RHITextureDesc& desc,
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

				RHISubresourceRange range = requested.value_or(RHISubresourceRange{});

				const uint32_t mipLevels = mipLevelsOf(desc);
				const uint32_t arraySize = arraySizeOf(desc);

				range.m_MipCount = resolveCount(range.m_BaseMip, range.m_MipCount, mipLevels);
				range.m_ArraySliceCount = resolveCount(
					range.m_BaseArraySlice,
					range.m_ArraySliceCount,
					arraySize);
				range.m_Aspects &= GetRHITextureAspects(desc);
				return range;
			};

		auto subresourceIndex = [&](const RHITextureDesc& desc,
			uint32_t mip,
			uint32_t arraySlice,
			RHITextureAspect aspect) noexcept
			{
				return mip + mipLevelsOf(desc) *
					(arraySlice + arraySizeOf(desc) * aspectIndexOf(desc, aspect));
			};

		auto decodeSubresource = [&](const RHITextureDesc& desc, uint32_t index) noexcept
			{
				const uint32_t mipLevels = mipLevelsOf(desc);
				const uint32_t arraySize = arraySizeOf(desc);
				return TextureSubresource
				{
					.m_Mip = index % mipLevels,
					.m_ArraySlice = (index / mipLevels) % arraySize,
					.m_Aspect = aspectFromIndex(desc, index / (mipLevels * arraySize)),
				};
			};

		auto forEachSubresource = [&](const RHITextureDesc& desc,
			const RHISubresourceRange& range,
			auto&& function) noexcept
			{
				for (const auto aspect : aspectOrder)
				{
					if (!Test(range.m_Aspects, aspect))
					{
						continue;
					}
					for (uint32_t arraySlice = range.m_BaseArraySlice;
						arraySlice < range.m_BaseArraySlice + range.m_ArraySliceCount;
						++arraySlice)
					{
						for (uint32_t mip = range.m_BaseMip; mip < range.m_BaseMip + range.m_MipCount; ++mip)
						{
							function(TextureSubresource{ mip, arraySlice, aspect });
						}
					}
				}
			};

		auto appendCoalescedBarriers = [&](RGVirtualResourceBase* virtualResource,
			const RHITextureDesc& desc,
			std::vector<TextureTransition>& transitions,
			std::vector<RGPassNode::BarrierIntent>& output) noexcept
			{
				struct TransitionGroup
				{
					RHIResourceState m_Before;
					RHIResourceState m_After;
					std::vector<TextureSubresource> m_Subresources;
				};

				std::vector<TransitionGroup> groups;
				for (const auto& transition : transitions)
				{
					auto group = std::ranges::find_if(groups,
						[&](const TransitionGroup& candidate)
						{
							return candidate.m_Before == transition.m_Before &&
								candidate.m_After == transition.m_After;
						});
					if (group == groups.end())
					{
						groups.push_back({ transition.m_Before, transition.m_After, {} });
						group = std::prev(groups.end());
					}
					group->m_Subresources.push_back(transition.m_Subresource);
				}

				const uint32_t fullSubresourceCount =
					mipLevelsOf(desc) * arraySizeOf(desc) * planeCountOf(desc);
				for (auto& group : groups)
				{
					std::unordered_set<uint32_t> uniqueSubresources;
					for (const auto& subresource : group.m_Subresources)
					{
						uniqueSubresources.insert(subresourceIndex(
							desc,
							subresource.m_Mip,
							subresource.m_ArraySlice,
							subresource.m_Aspect));
					}
					GGLAB_ASSERT_MSG(uniqueSubresources.size() == group.m_Subresources.size(),
						"Texture barrier coalescing received duplicate subresources.");
					if (uniqueSubresources.size() == fullSubresourceCount)
					{
						output.push_back(
							{
								.m_VirtualResource = virtualResource,
								.m_Before = group.m_Before,
								.m_After = group.m_After,
								.m_Subresources = std::nullopt,
							});
						continue;
					}

					std::ranges::sort(group.m_Subresources,
						[](const TextureSubresource& lhs, const TextureSubresource& rhs)
						{
							return std::tuple{ lhs.m_Aspect, lhs.m_ArraySlice, lhs.m_Mip } <
								std::tuple{ rhs.m_Aspect, rhs.m_ArraySlice, rhs.m_Mip };
						});

					std::vector<RHISubresourceRange> mipRanges;
					for (const auto& subresource : group.m_Subresources)
					{
						if (!mipRanges.empty())
						{
							auto& previous = mipRanges.back();
							if (previous.m_Aspects == subresource.m_Aspect &&
								previous.m_BaseArraySlice == subresource.m_ArraySlice &&
								previous.m_BaseMip + previous.m_MipCount == subresource.m_Mip)
							{
								++previous.m_MipCount;
								continue;
							}
						}
						mipRanges.push_back(
							{
								.m_BaseMip = subresource.m_Mip,
								.m_MipCount = 1,
								.m_BaseArraySlice = subresource.m_ArraySlice,
								.m_ArraySliceCount = 1,
								.m_Aspects = subresource.m_Aspect,
							});
					}

					std::ranges::sort(mipRanges,
						[](const RHISubresourceRange& lhs, const RHISubresourceRange& rhs)
						{
							return std::tuple{ lhs.m_Aspects, lhs.m_BaseMip, lhs.m_MipCount, lhs.m_BaseArraySlice } <
								std::tuple{ rhs.m_Aspects, rhs.m_BaseMip, rhs.m_MipCount, rhs.m_BaseArraySlice };
						});

					std::vector<RHISubresourceRange> arrayRanges;
					for (const auto& range : mipRanges)
					{
						if (!arrayRanges.empty())
						{
							auto& previous = arrayRanges.back();
							if (previous.m_Aspects == range.m_Aspects &&
								previous.m_BaseMip == range.m_BaseMip &&
								previous.m_MipCount == range.m_MipCount &&
								previous.m_BaseArraySlice + previous.m_ArraySliceCount == range.m_BaseArraySlice)
							{
								++previous.m_ArraySliceCount;
								continue;
							}
						}
						arrayRanges.push_back(range);
					}

					std::ranges::sort(arrayRanges,
						[](const RHISubresourceRange& lhs, const RHISubresourceRange& rhs)
						{
							return std::tuple{ lhs.m_BaseMip, lhs.m_MipCount, lhs.m_BaseArraySlice,
								lhs.m_ArraySliceCount, lhs.m_Aspects } <
								std::tuple{ rhs.m_BaseMip, rhs.m_MipCount, rhs.m_BaseArraySlice,
								rhs.m_ArraySliceCount, rhs.m_Aspects };
						});

					std::vector<RHISubresourceRange> mergedRanges;
					for (const auto& range : arrayRanges)
					{
						if (!mergedRanges.empty())
						{
							auto& previous = mergedRanges.back();
							if (previous.m_BaseMip == range.m_BaseMip &&
								previous.m_MipCount == range.m_MipCount &&
								previous.m_BaseArraySlice == range.m_BaseArraySlice &&
								previous.m_ArraySliceCount == range.m_ArraySliceCount)
							{
								previous.m_Aspects |= range.m_Aspects;
								continue;
							}
						}
						mergedRanges.push_back(range);
					}

					for (const auto& range : mergedRanges)
					{
						output.push_back(
							{
								.m_VirtualResource = virtualResource,
								.m_Before = group.m_Before,
								.m_After = group.m_After,
								.m_Subresources = range,
							});
					}

					uint64_t mergedSubresourceCount = 0;
					for (const auto& range : mergedRanges)
					{
						uint32_t rangeAspectCount = 0;
						for (const auto aspect : aspectOrder)
						{
							rangeAspectCount += Test(range.m_Aspects, aspect) ? 1u : 0u;
						}
						mergedSubresourceCount += static_cast<uint64_t>(range.m_MipCount) *
							range.m_ArraySliceCount * rangeAspectCount;
					}
					GGLAB_ASSERT_MSG(mergedSubresourceCount == uniqueSubresources.size(),
						"Texture barrier coalescing changed the covered subresource set.");
				}
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
					access.m_ResourceType,
					access.m_Stages);

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
					std::vector<TextureTransition> transitions;
					forEachSubresource(textureResource->m_Desc, range,
						[&](const TextureSubresource& subresource)
						{
							const uint32_t index = subresourceIndex(
								textureResource->m_Desc,
								subresource.m_Mip,
								subresource.m_ArraySlice,
								subresource.m_Aspect);
							auto [stateIter, inserted] = stateTracker.m_SubresourceStates.emplace(
								index,
								stateTracker.m_InitialState);
							auto& currentState = stateIter->second;
							if (NeedsRHIResourceBarrier(currentState, requiredState))
							{
								transitions.push_back({ subresource, currentState, requiredState });
							}
							currentState = requiredState;
						});
					appendCoalescedBarriers(
						virtualResource,
						textureResource->m_Desc,
						transitions,
						passNode.m_PreBarriers);
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
			if (virtualResource->m_RefCount == 0 && !virtualResource->m_ExportPass.IsValid())
			{
				continue;
			}
			const RGPassNodeIndex finalBarrierPass = virtualResource->m_ExportPass.IsValid() ?
				virtualResource->m_ExportPass :
				virtualResource->m_LastUser;
			GGLAB_ASSERT_MSG(finalBarrierPass.IsValid(), "RenderGraph final barrier requires an owning pass.");

			if (virtualResource->m_ResourceType == RGResourceType::RGTexture)
			{
				const auto* textureResource =
					static_cast<const RGVirtualResource<RGTextureResource>*>(virtualResource);
				auto& stateTracker = textureStates.at(virtualResource);
				std::optional<RHISubresourceRange> normalizedFinalRange;
				if (virtualResource->m_Imported && virtualResource->m_FinalBarrierState)
				{
					normalizedFinalRange = normalizeTextureRange(
						textureResource->m_Desc,
						virtualResource->m_FinalBarrierSubresources);
					const auto& range = *normalizedFinalRange;
					forEachSubresource(textureResource->m_Desc, range,
						[&](const TextureSubresource& subresource)
						{
							stateTracker.m_SubresourceStates.try_emplace(
								subresourceIndex(textureResource->m_Desc,
									subresource.m_Mip,
									subresource.m_ArraySlice,
									subresource.m_Aspect),
								stateTracker.m_InitialState);
						});
				}

				std::vector<TextureTransition> transitions;
				for (auto& [subresourceIndex, currentState] : stateTracker.m_SubresourceStates)
				{
					const TextureSubresource subresource = decodeSubresource(
						textureResource->m_Desc,
						subresourceIndex);
					RHIResourceState requiredFinalState = virtualResource->m_Imported ?
						virtualResource->m_InitialBarrierState :
						CommonRHIResourceState();

					if (normalizedFinalRange)
					{
						const auto& range = *normalizedFinalRange;
						const bool inFinalRange =
							subresource.m_Mip >= range.m_BaseMip &&
							subresource.m_Mip < range.m_BaseMip + range.m_MipCount &&
							subresource.m_ArraySlice >= range.m_BaseArraySlice &&
							subresource.m_ArraySlice < range.m_BaseArraySlice + range.m_ArraySliceCount &&
							Test(range.m_Aspects, subresource.m_Aspect);
						if (inFinalRange)
						{
							requiredFinalState = *virtualResource->m_FinalBarrierState;
						}
					}

					if (!NeedsRHIResourceBarrier(currentState, requiredFinalState))
					{
						continue;
					}

					transitions.push_back({ subresource, currentState, requiredFinalState });
					currentState = requiredFinalState;
				}
				appendCoalescedBarriers(
					virtualResource,
					textureResource->m_Desc,
					transitions,
					m_PassNodes[finalBarrierPass.Value()].m_PostBarriers);
				continue;
			}

			const RHIResourceState requiredFinalState = virtualResource->m_Imported ?
				virtualResource->m_FinalBarrierState.value_or(virtualResource->m_InitialBarrierState) :
				CommonRHIResourceState();
			auto& currentState = bufferStates.at(virtualResource);
			if (!NeedsRHIResourceBarrier(currentState, requiredFinalState))
			{
				continue;
			}
			m_PassNodes[finalBarrierPass.Value()].m_PostBarriers.push_back(
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
