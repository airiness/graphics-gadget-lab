#include "Core/Precompiled.h"
#include "Graphics/Asset/Residency/AssetResidencyController.h"

namespace gglab
{
	AssetResidencyPlan AssetResidencyController::BuildPlan(
		const AssetResidencyInventorySnapshot& snapshot,
		const AssetResidencyConfig& config) const noexcept
	{
		AssetResidencyPlan plan{
			.m_SnapshotFrame = snapshot.m_Frame,
			.m_LogicalResidentBytes = snapshot.m_LogicalResidentBytes,
			.m_ProjectedResidentBytes = snapshot.m_LogicalResidentBytes,
		};
		if (!config.m_EnableAutomaticEviction || config.m_MaxEvictionsPerFrame == 0 ||
			snapshot.m_LogicalResidentBytes <= config.m_HighWatermarkBytes)
		{
			return plan;
		}

		std::vector<const AssetResidencyInventoryEntry*> candidates;
		candidates.reserve(snapshot.m_Entries.size());
		for (const AssetResidencyInventoryEntry& entry : snapshot.m_Entries)
		{
			if (IsEvictionCandidate(entry, snapshot.m_Frame, config))
			{
				candidates.push_back(&entry);
			}
		}
		std::ranges::sort(candidates,
			[](const AssetResidencyInventoryEntry* lhs,
				const AssetResidencyInventoryEntry* rhs) noexcept
			{
				return std::tie(
					lhs->m_Stamp.m_LastUsedFrame,
					lhs->m_Stamp.m_ContentVersion.m_Key.m_Kind,
					lhs->m_Stamp.m_ContentVersion.m_Key.m_StableId) <
					std::tie(
						rhs->m_Stamp.m_LastUsedFrame,
						rhs->m_Stamp.m_ContentVersion.m_Key.m_Kind,
						rhs->m_Stamp.m_ContentVersion.m_Key.m_StableId);
			});

		for (const AssetResidencyInventoryEntry* candidate : candidates)
		{
			if (plan.m_ProjectedResidentBytes <= config.m_LowWatermarkBytes ||
				plan.m_Actions.size() >= config.m_MaxEvictionsPerFrame)
			{
				break;
			}
			plan.m_Actions.push_back({
				.m_Operation = AssetResidencyOperationKind::Evict,
				.m_ExpectedStamp = candidate->m_Stamp,
				.m_Reason = AssetResidencyActionReason::BudgetPressure,
				.m_EstimatedBytes = candidate->m_EstimatedBytes,
			});
			plan.m_ProjectedResidentBytes =
				plan.m_ProjectedResidentBytes > candidate->m_EstimatedBytes ?
					plan.m_ProjectedResidentBytes - candidate->m_EstimatedBytes : 0;
		}
		return plan;
	}

	bool AssetResidencyController::IsEvictionCandidate(
		const AssetResidencyInventoryEntry& entry,
		uint64_t snapshotFrame,
		const AssetResidencyConfig& config) noexcept
	{
		const uint64_t lastUsedFrame = entry.m_Stamp.m_LastUsedFrame;
		const uint64_t unusedFrames = lastUsedFrame == 0 ?
			snapshotFrame : snapshotFrame > lastUsedFrame ? snapshotFrame - lastUsedFrame : 0;
		return entry.m_Stamp.m_ContentVersion.IsValid() &&
			(entry.m_Stamp.m_ContentVersion.m_Key.m_Kind == AssetKind::Mesh ||
				entry.m_Stamp.m_ContentVersion.m_Key.m_Kind == AssetKind::Texture) &&
			entry.m_Stamp.m_State == AssetState::Ready &&
			entry.m_Stamp.m_ResidencyState == AssetResidencyState::Resident &&
			entry.m_Stamp.m_ResidencyPolicy == AssetResidencyPolicy::Cacheable &&
			entry.m_EstimatedBytes != 0 && entry.m_HasReloadSource &&
			!entry.m_IsReserved && !entry.m_HasActiveInterest &&
			!entry.m_HasPublicationRetain && !entry.m_HasPinnedDependentModel &&
			unusedFrames >= config.m_MinUnusedFrames;
	}
}
