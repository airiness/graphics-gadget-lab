#include "Core/Precompiled.h"
#include "Graphics/RenderGraph/RGBarrierPlanner.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RHI/RHISubresourceUtils.h"

namespace gglab
{
	namespace
	{
		struct TrackedResourceState
		{
			RHIResourceState m_State = CommonRHIResourceState();
			std::optional<RGDependencyAccess> m_LastOrderedUavAccess = std::nullopt;
		};

		void RecordAccess(TrackedResourceState& trackedState, const RHIResourceState& requiredState,
			bool synchronized) noexcept
		{
			if (synchronized)
			{
				trackedState.m_State = requiredState;
				return;
			}

			GGLAB_ASSERT_MSG(!NeedsRHIResourceTransition(trackedState.m_State, requiredState),
				"Unsynchronized access must preserve the persistent resource state.");
			trackedState.m_State.m_Stages |= requiredState.m_Stages;
		}

		struct TextureStateTracker
		{
			RHIResourceState m_InitialState = CommonRHIResourceState();
			std::unordered_map<uint32_t, TrackedResourceState> m_SubresourceStates;
		};

		struct TextureSubresource
		{
			uint32_t m_Mip = 0;
			uint32_t m_ArraySlice = 0;
			RHITextureAspect m_Aspect = RHITextureAspect::Color;
		};

		struct TextureBarrierRecord
		{
			TextureSubresource m_Subresource;
			RHIResourceState m_Before;
			RHIResourceState m_After;
		};

		constexpr std::array TextureAspectOrder = {
			RHITextureAspect::Color,
			RHITextureAspect::Depth,
			RHITextureAspect::Stencil,
		};

		uint32_t PlaneCountOf(const RHITextureDesc& desc) noexcept
		{
			return GetRHITexturePlaneCount(desc);
		}

		RHITextureDesc CompiledTextureDesc(const RGCompiledResource& resource) noexcept
		{
			GGLAB_ASSERT_MSG(resource.m_ResourceType == RGResourceType::RGTexture,
				"RenderGraph compiled texture description requires a texture resource.");
			const auto* texture =
				static_cast<const RGVirtualResource<RGTextureResource>*>(resource.m_Resource);
			RHITextureDesc desc = texture->m_Desc;
			desc.m_Usage = static_cast<RHITextureUsage>(resource.m_UsageBits);
			return desc;
		}

		uint32_t AspectIndexOf(const RHITextureDesc& desc, RHITextureAspect target) noexcept
		{
			uint32_t index = 0;
			const RHITextureAspect aspects = GetRHITextureAspects(desc);
			for (const auto aspect : TextureAspectOrder)
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
		}

		RHITextureAspect AspectFromIndex(const RHITextureDesc& desc, uint32_t targetIndex) noexcept
		{
			uint32_t index = 0;
			const RHITextureAspect aspects = GetRHITextureAspects(desc);
			for (const auto aspect : TextureAspectOrder)
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
		}

		uint32_t SubresourceIndex(const RHITextureDesc& desc, uint32_t mip, uint32_t arraySlice,
			RHITextureAspect aspect) noexcept
		{
			return mip +
				GetRHITextureMipLevelCount(desc) *
				(arraySlice + GetRHITextureArraySize(desc) * AspectIndexOf(desc, aspect));
		}

		TextureSubresource DecodeSubresource(const RHITextureDesc& desc, uint32_t index) noexcept
		{
			const uint32_t mipLevels = GetRHITextureMipLevelCount(desc);
			const uint32_t arraySize = GetRHITextureArraySize(desc);
			return TextureSubresource{
				.m_Mip = index % mipLevels,
				.m_ArraySlice = (index / mipLevels) % arraySize,
				.m_Aspect = AspectFromIndex(desc, index / (mipLevels * arraySize)),
			};
		}

		template <typename Func>
		void ForEachSubresource(
			const RHITextureDesc& desc, const RHISubresourceRange& range, Func&& function) noexcept
		{
			for (const auto aspect : TextureAspectOrder)
			{
				if (!Test(range.m_Aspects, aspect))
				{
					continue;
				}
				for (uint32_t arraySlice = range.m_BaseArraySlice;
					arraySlice < range.m_BaseArraySlice + range.m_ArraySliceCount; ++arraySlice)
				{
					for (uint32_t mip = range.m_BaseMip; mip < range.m_BaseMip + range.m_MipCount;
						++mip)
					{
						function(TextureSubresource{ mip, arraySlice, aspect });
					}
				}
			}
		}

		void AppendCoalescedTextureBarriers(RGVirtualResourceIndex resourceIndex,
			const RHITextureDesc& desc, std::vector<TextureBarrierRecord>& barriers,
			std::vector<RGBarrierIntent>& output, RGBarrierKind kind = RGBarrierKind::Transition,
			RGBarrierReason reason = RGBarrierReason::AccessTransition) noexcept
		{
			struct BarrierGroup
			{
				RHIResourceState m_Before;
				RHIResourceState m_After;
				std::vector<TextureSubresource> m_Subresources;
			};

			std::vector<BarrierGroup> groups;
			for (const auto& barrier : barriers)
			{
				auto group = std::ranges::find_if(groups,
					[&](const BarrierGroup& candidate)
					{
						return candidate.m_Before == barrier.m_Before &&
							candidate.m_After == barrier.m_After;
					});
				if (group == groups.end())
				{
					groups.push_back({ barrier.m_Before, barrier.m_After, {} });
					group = std::prev(groups.end());
				}
				group->m_Subresources.push_back(barrier.m_Subresource);
			}

			const uint32_t fullSubresourceCount = GetRHITextureMipLevelCount(desc) *
				GetRHITextureArraySize(desc) * PlaneCountOf(desc);
			for (auto& group : groups)
			{
				std::unordered_set<uint32_t> uniqueSubresources;
				for (const auto& subresource : group.m_Subresources)
				{
					uniqueSubresources.insert(SubresourceIndex(
						desc, subresource.m_Mip, subresource.m_ArraySlice, subresource.m_Aspect));
				}
				GGLAB_ASSERT_MSG(uniqueSubresources.size() == group.m_Subresources.size(),
					"Texture barrier coalescing received duplicate subresources.");
				if (uniqueSubresources.size() == fullSubresourceCount)
				{
					output.push_back({
						.m_Resource = resourceIndex,
						.m_Kind = kind,
						.m_Reason = reason,
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
					mipRanges.push_back({
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
						return
							std::tuple{ lhs.m_Aspects, lhs.m_BaseMip,
								lhs.m_MipCount, lhs.m_BaseArraySlice } <
							std::tuple{ rhs.m_Aspects, rhs.m_BaseMip,
								rhs.m_MipCount, rhs.m_BaseArraySlice };
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
							previous.m_BaseArraySlice + previous.m_ArraySliceCount ==
							range.m_BaseArraySlice)
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
						return
							std::tuple{ lhs.m_BaseMip, lhs.m_MipCount, lhs.m_BaseArraySlice,
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
					output.push_back({
						.m_Resource = resourceIndex,
						.m_Kind = kind,
						.m_Reason = reason,
						.m_Before = group.m_Before,
						.m_After = group.m_After,
						.m_Subresources = range,
						});
				}

				uint64_t mergedSubresourceCount = 0;
				for (const auto& range : mergedRanges)
				{
					uint32_t rangeAspectCount = 0;
					for (const auto aspect : TextureAspectOrder)
					{
						rangeAspectCount += Test(range.m_Aspects, aspect) ? 1u : 0u;
					}
					mergedSubresourceCount += static_cast<uint64_t>(range.m_MipCount) *
						range.m_ArraySliceCount * rangeAspectCount;
				}
				GGLAB_ASSERT_MSG(mergedSubresourceCount == uniqueSubresources.size(),
					"Texture barrier coalescing changed the covered subresource set.");
			}
		}
	}

	RGBarrierPlanner::RGBarrierPlanner(std::vector<RGCompiledPass>& passes,
		std::vector<RGCompiledResource>& resources,
		const std::vector<RGPassNodeIndex>& executionOrder) noexcept :
		m_Passes(passes), m_Resources(resources), m_ExecutionOrder(executionOrder)
	{
	}

	void RGBarrierPlanner::Build() noexcept
	{
		std::unordered_map<RGVirtualResourceBase*, TextureStateTracker> textureStates;
		std::unordered_map<RGVirtualResourceBase*, TrackedResourceState> bufferStates;
		for (const auto& resource : m_Resources)
		{
			auto* virtualResource = resource.m_Resource;
			if (virtualResource->m_ResourceType == RGResourceType::RGTexture)
			{
				textureStates.emplace(virtualResource,
					TextureStateTracker{ .m_InitialState = resource.m_InitialState });
			}
			else
			{
				bufferStates.emplace(virtualResource,
					TrackedResourceState{
						.m_State = resource.m_InitialState,
						.m_LastOrderedUavAccess = HasUavAccess(resource.m_InitialState)
							? std::optional{RGDependencyAccess::ReadWrite}
							: std::nullopt,
					});
			}
		}

		for (const auto passNodeIndex : m_ExecutionOrder)
		{
			auto& passNode = m_Passes[passNodeIndex.Value()];
			for (const auto& access : passNode.m_Accesses)
			{
				const RHIResourceState requiredState = ToRHIResourceState(
					access.m_AccessValue, access.m_ResourceType, access.m_Stages);

				const auto& compiledResource = m_Resources[access.m_Resource.Value()];
				auto* virtualResource = compiledResource.m_Resource;
				if (!virtualResource)
				{
					continue;
				}

				if (virtualResource->m_ResourceType == RGResourceType::RGTexture)
				{
					const RHITextureDesc textureDesc = CompiledTextureDesc(compiledResource);
					const RHISubresourceRange range =
						NormalizeTextureSubresourceRange(textureDesc, access.m_Subresources);
					auto& stateTracker = textureStates.at(virtualResource);
					std::vector<TextureBarrierRecord> transitions;
					std::vector<TextureBarrierRecord> uavBarriers;
					ForEachSubresource(textureDesc, range,
						[&](const TextureSubresource& subresource)
						{
							const uint32_t index = SubresourceIndex(textureDesc, subresource.m_Mip,
								subresource.m_ArraySlice, subresource.m_Aspect);
							auto stateIter =
								stateTracker.m_SubresourceStates
								.emplace(index,
									TrackedResourceState{
										.m_State = stateTracker.m_InitialState,
										.m_LastOrderedUavAccess =
											HasUavAccess(stateTracker.m_InitialState)
												? std::optional{RGDependencyAccess::ReadWrite}
												: std::nullopt,
									})
								.first;
							auto& trackedState = stateIter->second;
							if (NeedsRHIResourceTransition(trackedState.m_State, requiredState))
							{
								transitions.push_back(
									{ subresource, trackedState.m_State, requiredState });
							}
							else if (HasUavAccess(requiredState) &&
								trackedState.m_LastOrderedUavAccess &&
								NeedsOrderedUavBarrier(*trackedState.m_LastOrderedUavAccess,
									RGOrderingRequirement::Ordered, access.m_DependencyAccess,
									access.m_Ordering))
							{
								uavBarriers.push_back({
									.m_Subresource = subresource,
									.m_Before = trackedState.m_State,
									.m_After = requiredState,
									});
							}
						});
					AppendCoalescedTextureBarriers(
						access.m_Resource, textureDesc, transitions, passNode.m_PreBarriers);
					AppendCoalescedTextureBarriers(access.m_Resource, textureDesc, uavBarriers,
						passNode.m_PreBarriers, RGBarrierKind::Uav,
						RGBarrierReason::OrderedStorageHazard);
					ForEachSubresource(textureDesc, range,
						[&](const TextureSubresource& subresource)
						{
							const uint32_t index = SubresourceIndex(textureDesc, subresource.m_Mip,
								subresource.m_ArraySlice, subresource.m_Aspect);
							auto& trackedState = stateTracker.m_SubresourceStates.at(index);
							const bool transitioned =
								NeedsRHIResourceTransition(trackedState.m_State, requiredState);
							const bool orderedUavHazard =
								!transitioned && HasUavAccess(requiredState) &&
								trackedState.m_LastOrderedUavAccess &&
								NeedsOrderedUavBarrier(*trackedState.m_LastOrderedUavAccess,
									RGOrderingRequirement::Ordered, access.m_DependencyAccess,
									access.m_Ordering);
							RecordAccess(
								trackedState, requiredState, transitioned || orderedUavHazard);
							if (transitioned || !HasUavAccess(requiredState))
							{
								trackedState.m_LastOrderedUavAccess.reset();
							}
							if (HasUavAccess(requiredState) &&
								access.m_Ordering == RGOrderingRequirement::Ordered)
							{
								trackedState.m_LastOrderedUavAccess = access.m_DependencyAccess;
							}
						});
					continue;
				}

				auto& trackedState = bufferStates.at(virtualResource);
				const bool transitioned =
					NeedsRHIResourceTransition(trackedState.m_State, requiredState);
				bool synchronized = transitioned;
				if (transitioned)
				{
					passNode.m_PreBarriers.push_back({
						.m_Resource = access.m_Resource,
						.m_Kind = RGBarrierKind::Transition,
						.m_Reason = RGBarrierReason::AccessTransition,
						.m_Before = trackedState.m_State,
						.m_After = requiredState,
						});
				}
				else if (HasUavAccess(requiredState) && trackedState.m_LastOrderedUavAccess &&
					NeedsOrderedUavBarrier(*trackedState.m_LastOrderedUavAccess,
						RGOrderingRequirement::Ordered, access.m_DependencyAccess,
						access.m_Ordering))
				{
					RHIResourceState beforeState = trackedState.m_State;
					passNode.m_PreBarriers.push_back({
						.m_Resource = access.m_Resource,
						.m_Kind = RGBarrierKind::Uav,
						.m_Reason = RGBarrierReason::OrderedStorageHazard,
						.m_Before = beforeState,
						.m_After = requiredState,
						});
					trackedState.m_LastOrderedUavAccess.reset();
					synchronized = true;
				}
				RecordAccess(trackedState, requiredState, synchronized);
				if (transitioned || !HasUavAccess(requiredState))
				{
					trackedState.m_LastOrderedUavAccess.reset();
				}
				if (HasUavAccess(requiredState) &&
					access.m_Ordering == RGOrderingRequirement::Ordered)
				{
					trackedState.m_LastOrderedUavAccess = access.m_DependencyAccess;
				}
			}
		}

		for (const auto& resource : m_Resources)
		{
			auto* virtualResource = resource.m_Resource;
			if (resource.m_RefCount == 0 && !resource.m_ExportPass.IsValid())
			{
				continue;
			}
			const RGPassNodeIndex finalBarrierPass =
				resource.m_ExportPass.IsValid() ? resource.m_ExportPass : resource.m_LastUser;
			GGLAB_ASSERT_MSG(
				finalBarrierPass.IsValid(), "RenderGraph final barrier requires an owning pass.");

			if (virtualResource->m_ResourceType == RGResourceType::RGTexture)
			{
				const RHITextureDesc textureDesc = CompiledTextureDesc(resource);
				auto& stateTracker = textureStates.at(virtualResource);
				std::optional<RHISubresourceRange> normalizedFinalRange;
				if (resource.m_Imported && resource.m_FinalState)
				{
					normalizedFinalRange =
						NormalizeTextureSubresourceRange(textureDesc, resource.m_FinalSubresources);
					const auto& range = *normalizedFinalRange;
					ForEachSubresource(textureDesc, range,
						[&](const TextureSubresource& subresource)
						{
							stateTracker.m_SubresourceStates.try_emplace(
								SubresourceIndex(textureDesc, subresource.m_Mip,
									subresource.m_ArraySlice, subresource.m_Aspect),
								TrackedResourceState{
									.m_State = stateTracker.m_InitialState,
									.m_LastOrderedUavAccess =
										HasUavAccess(stateTracker.m_InitialState)
											? std::optional{RGDependencyAccess::ReadWrite}
											: std::nullopt,
								});
						});
				}

				std::vector<TextureBarrierRecord> transitions;
				for (auto& [subresourceIndex, trackedState] : stateTracker.m_SubresourceStates)
				{
					const TextureSubresource subresource =
						DecodeSubresource(textureDesc, subresourceIndex);
					RHIResourceState requiredFinalState =
						resource.m_Imported ? resource.m_InitialState : CommonRHIResourceState();

					if (normalizedFinalRange)
					{
						const auto& range = *normalizedFinalRange;
						const bool inFinalRange =
							subresource.m_Mip >= range.m_BaseMip &&
							subresource.m_Mip < range.m_BaseMip + range.m_MipCount &&
							subresource.m_ArraySlice >= range.m_BaseArraySlice &&
							subresource.m_ArraySlice <
							range.m_BaseArraySlice + range.m_ArraySliceCount &&
							Test(range.m_Aspects, subresource.m_Aspect);
						if (inFinalRange)
						{
							requiredFinalState = *resource.m_FinalState;
						}
					}

					if (!NeedsRHIResourceTransition(trackedState.m_State, requiredFinalState))
					{
						continue;
					}

					transitions.push_back({ subresource, trackedState.m_State, requiredFinalState });
					trackedState.m_State = requiredFinalState;
					trackedState.m_LastOrderedUavAccess.reset();
				}
				const size_t transitionStart =
					m_Passes[finalBarrierPass.Value()].m_PostBarriers.size();
				AppendCoalescedTextureBarriers(resource.m_Declaration, textureDesc, transitions,
					m_Passes[finalBarrierPass.Value()].m_PostBarriers);
				auto& postBarriers = m_Passes[finalBarrierPass.Value()].m_PostBarriers;
				for (size_t barrierIndex = transitionStart; barrierIndex < postBarriers.size();
					++barrierIndex)
				{
					postBarriers[barrierIndex].m_Reason = RGBarrierReason::FinalStateTransition;
				}
				continue;
			}

			const RHIResourceState requiredFinalState =
				resource.m_Imported ? resource.m_FinalState.value_or(resource.m_InitialState)
				: CommonRHIResourceState();
			auto& trackedState = bufferStates.at(virtualResource);
			if (!NeedsRHIResourceTransition(trackedState.m_State, requiredFinalState))
			{
				continue;
			}
			m_Passes[finalBarrierPass.Value()].m_PostBarriers.push_back({
				.m_Resource = resource.m_Declaration,
				.m_Kind = RGBarrierKind::Transition,
				.m_Reason = RGBarrierReason::FinalStateTransition,
				.m_Before = trackedState.m_State,
				.m_After = requiredFinalState,
				});
			trackedState.m_State = requiredFinalState;
			trackedState.m_LastOrderedUavAccess.reset();
		}
	}
}
