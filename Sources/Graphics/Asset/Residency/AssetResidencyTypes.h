#pragma once
#include "Graphics/Asset/AssetIdentity.h"
#include "Graphics/GraphicsTypes.h"

#include <cstdint>
#include <vector>

namespace gglab
{
	struct AssetResidencyConfig
	{
		bool m_EnableAutomaticEviction = false;
		uint64_t m_HighWatermarkBytes = 512ull * 1024ull * 1024ull;
		uint64_t m_LowWatermarkBytes = 384ull * 1024ull * 1024ull;
		uint64_t m_MinUnusedFrames = 120;
		uint32_t m_MaxEvictionsPerFrame = 8;
	};

	enum class AssetResidencyOperationKind : uint8_t
	{
		Evict,
		Reload,
	};

	enum class AssetResidencyActionReason : uint8_t
	{
		BudgetPressure,
	};

	struct AssetStateStamp
	{
		AssetContentVersion m_ContentVersion{};
		uint64_t m_ResidencyEpoch = 0;
		uint64_t m_LastUsedFrame = 0;
		AssetState m_State = AssetState::Unloaded;
		AssetContentState m_ContentState = AssetContentState::Unloaded;
		AssetResidencyState m_ResidencyState = AssetResidencyState::NonResident;
		AssetResidencyPolicy m_ResidencyPolicy = AssetResidencyPolicy::Cacheable;

		friend bool operator==(const AssetStateStamp&, const AssetStateStamp&) = default;
	};

	struct AssetResidencyInventoryEntry
	{
		AssetStateStamp m_Stamp{};
		uint64_t m_EstimatedBytes = 0;
		bool m_IsReserved = false;
		bool m_HasReloadSource = false;
		bool m_HasActiveInterest = false;
		bool m_HasPublicationRetain = false;
		bool m_HasPinnedDependentModel = false;

		friend bool operator==(
			const AssetResidencyInventoryEntry&,
			const AssetResidencyInventoryEntry&) = default;
	};

	struct AssetResidencyInventorySnapshot
	{
		uint64_t m_Frame = 0;
		uint64_t m_LogicalResidentBytes = 0;
		std::vector<AssetResidencyInventoryEntry> m_Entries;

		friend bool operator==(
			const AssetResidencyInventorySnapshot&,
			const AssetResidencyInventorySnapshot&) = default;
	};

	struct AssetResidencyAction
	{
		AssetResidencyOperationKind m_Operation = AssetResidencyOperationKind::Evict;
		AssetStateStamp m_ExpectedStamp{};
		AssetResidencyActionReason m_Reason = AssetResidencyActionReason::BudgetPressure;
		uint64_t m_EstimatedBytes = 0;
		bool m_Attempted = false;

		friend bool operator==(const AssetResidencyAction&, const AssetResidencyAction&) = default;
	};

	struct AssetResidencyPlan
	{
		uint64_t m_SnapshotFrame = 0;
		uint64_t m_LogicalResidentBytes = 0;
		uint64_t m_ProjectedResidentBytes = 0;
		std::vector<AssetResidencyAction> m_Actions;
		bool m_Consumed = false;

		friend bool operator==(const AssetResidencyPlan&, const AssetResidencyPlan&) = default;
	};

	struct AssetResidencyStatistics
	{
		AssetResidencyConfig m_Config{};
		uint64_t m_LogicalResidentBytes = 0;
		uint64_t m_PendingEvictionBytes = 0;
		uint32_t m_PendingEvictionCount = 0;
		uint32_t m_ReloadingAssetCount = 0;
		uint64_t m_EvictionCount = 0;
		uint64_t m_EvictedBytes = 0;
		uint64_t m_EvictionCancellationCount = 0;
		uint64_t m_ReloadRequestCount = 0;
		uint64_t m_ReloadCoalescedCount = 0;
		uint32_t m_LastFrameReloadRequestCount = 0;
		uint32_t m_ReloadRequestHighWatermark = 0;
		uint64_t m_PlanningCount = 0;
		uint64_t m_LastPlanFrame = 0;
		uint32_t m_LastPlannedActionCount = 0;
		uint64_t m_LastPlannedBytes = 0;
	};
}
