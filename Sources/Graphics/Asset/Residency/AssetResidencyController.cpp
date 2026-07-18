#include "Core/Precompiled.h"
#include "Graphics/Asset/Residency/AssetResidencyController.h"

namespace gglab
{
	void AssetResidencyController::SetConfig(const AssetResidencyConfig& config) noexcept
	{
		m_Statistics.m_Config = config;
		m_Statistics.m_Config.m_LowWatermarkBytes = std::min(
			m_Statistics.m_Config.m_LowWatermarkBytes,
			m_Statistics.m_Config.m_HighWatermarkBytes);
	}

	const AssetResidencyConfig& AssetResidencyController::GetConfig() const noexcept
	{
		return m_Statistics.m_Config;
	}

	AssetResidencyStatistics AssetResidencyController::GetStatistics(
		uint64_t logicalResidentBytes,
		uint64_t pendingEvictionBytes,
		uint32_t pendingEvictionCount,
		uint32_t reloadingAssetCount) const noexcept
	{
		AssetResidencyStatistics statistics = m_Statistics;
		statistics.m_LogicalResidentBytes = logicalResidentBytes;
		statistics.m_PendingEvictionBytes = pendingEvictionBytes;
		statistics.m_PendingEvictionCount = pendingEvictionCount;
		statistics.m_ReloadingAssetCount = reloadingAssetCount;
		return statistics;
	}

	AssetResidencyOperation AssetResidencyController::BeginResidencyOperation(
		AssetLifecycle& lifecycle,
		AssetContentVersion contentVersion,
		AssetResidencyOperationKind kind) noexcept
	{
		GGLAB_ASSERT(contentVersion.IsValid());
		GGLAB_ASSERT(lifecycle.m_ContentGeneration == contentVersion.m_ContentGeneration);
		if (!contentVersion.IsValid() ||
			lifecycle.m_ContentGeneration != contentVersion.m_ContentGeneration)
		{
			return {};
		}

		const uint64_t serial = m_NextOperationSerial++;
		GGLAB_ASSERT(serial != 0);
		if (serial == 0)
		{
			return {};
		}
		lifecycle.m_ResidencyOperationSerial = serial;
		++m_Statistics.m_OperationCount;
		return {
			.m_Token = MakeAssetOperationToken(contentVersion, serial),
			.m_Kind = kind,
		};
	}

	bool AssetResidencyController::IsCurrentOperation(
		const AssetLifecycle& lifecycle,
		const AssetResidencyOperation& operation) noexcept
	{
		return operation.IsValid() && IsCurrentOperation(lifecycle, operation.m_Token);
	}

	bool AssetResidencyController::IsCurrentOperation(
		const AssetLifecycle& lifecycle,
		const AssetOperationToken& operation) noexcept
	{
		return operation.IsValid() &&
			lifecycle.m_ContentGeneration ==
				operation.m_ContentVersion.m_ContentGeneration &&
			lifecycle.m_ResidencyOperationSerial ==
				operation.m_OperationSerial;
	}

	void AssetResidencyController::CompleteResidencyOperation(
		AssetLifecycle& lifecycle,
		const AssetResidencyOperation& operation) noexcept
	{
		if (operation.IsValid())
		{
			CompleteResidencyOperation(lifecycle, operation.m_Token);
		}
	}

	void AssetResidencyController::CompleteResidencyOperation(
		AssetLifecycle& lifecycle,
		const AssetOperationToken& operation) noexcept
	{
		if (IsCurrentOperation(lifecycle, operation))
		{
			lifecycle.m_ResidencyOperationSerial = 0;
		}
	}

	void AssetResidencyController::InvalidateResidencyOperation(
		AssetLifecycle& lifecycle) noexcept
	{
		lifecycle.m_ResidencyOperationSerial = 0;
	}

	AssetResidencyPlan AssetResidencyController::BuildPlan(
		const AssetResidencyInventorySnapshot& snapshot) const noexcept
	{
		const AssetResidencyConfig& config = m_Statistics.m_Config;
		AssetResidencyPlan plan{
			.m_SnapshotFrame = snapshot.m_Frame,
			.m_LogicalResidentBytes = snapshot.m_LogicalResidentBytes,
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

		uint64_t projectedResidentBytes = snapshot.m_LogicalResidentBytes;
		for (const AssetResidencyInventoryEntry* candidate : candidates)
		{
			if (projectedResidentBytes <= config.m_LowWatermarkBytes ||
				plan.m_Actions.size() >= config.m_MaxEvictionsPerFrame)
			{
				break;
			}
			plan.m_Actions.push_back({
				.m_ExpectedStamp = candidate->m_Stamp,
				.m_EstimatedBytes = candidate->m_EstimatedBytes,
			});
			projectedResidentBytes =
				projectedResidentBytes > candidate->m_EstimatedBytes ?
					projectedResidentBytes - candidate->m_EstimatedBytes : 0;
		}
		return plan;
	}

	bool AssetResidencyController::StillEligible(
		const AssetResidencyAction& action,
		const AssetResidencyInventoryEntry& currentEntry,
		uint64_t currentFrame,
		uint64_t projectedResidentBytes) const noexcept
	{
		return m_Statistics.m_Config.m_EnableAutomaticEviction &&
			projectedResidentBytes > m_Statistics.m_Config.m_LowWatermarkBytes &&
			currentEntry.m_Stamp == action.m_ExpectedStamp &&
			currentEntry.m_EstimatedBytes == action.m_EstimatedBytes &&
			IsEvictionCandidate(currentEntry, currentFrame, m_Statistics.m_Config);
	}

	void AssetResidencyController::MarkAssetUsed(
		AssetLifecycle& lifecycle,
		uint64_t currentFrame) noexcept
	{
		if (lifecycle.m_ResidencyState == AssetResidencyState::Resident &&
			currentFrame != 0 && lifecycle.m_LastUsedFrame != currentFrame)
		{
			lifecycle.m_LastUsedFrame = currentFrame;
			++lifecycle.m_UseCount;
		}
	}

	bool AssetResidencyController::SetResidencyPolicy(
		AssetLifecycle& lifecycle,
		AssetResidencyPolicy policy,
		bool isReserved) noexcept
	{
		if (isReserved && policy != AssetResidencyPolicy::Pinned)
		{
			return false;
		}
		lifecycle.m_ResidencyPolicy = policy;
		return true;
	}

	void AssetResidencyController::RecordPlan(const AssetResidencyPlan& plan) noexcept
	{
		++m_Statistics.m_PlanningCount;
		m_Statistics.m_LastPlanFrame = plan.m_SnapshotFrame;
		m_Statistics.m_LastPlannedActionCount = static_cast<uint32_t>(plan.m_Actions.size());
		m_Statistics.m_LastPlannedBytes = 0;
		for (const AssetResidencyAction& action : plan.m_Actions)
		{
			m_Statistics.m_LastPlannedBytes += action.m_EstimatedBytes;
		}
	}

	void AssetResidencyController::RecordEviction(
		bool cancelled,
		uint64_t residentBytes) noexcept
	{
		if (cancelled)
		{
			++m_Statistics.m_EvictionCancellationCount;
			return;
		}
		++m_Statistics.m_EvictionCount;
		m_Statistics.m_EvictedBytes += residentBytes;
	}

	void AssetResidencyController::RecordReloadRequest(bool coalesced) noexcept
	{
		++m_Statistics.m_ReloadRequestCount;
		++m_CurrentFrameReloadRequestCount;
		m_Statistics.m_ReloadCoalescedCount += coalesced ? 1 : 0;
	}

	void AssetResidencyController::RecordReloadCoalesced() noexcept
	{
		++m_Statistics.m_ReloadCoalescedCount;
	}

	void AssetResidencyController::RecordRevalidationRejection() noexcept
	{
		++m_Statistics.m_RevalidationRejectionCount;
	}

	void AssetResidencyController::RecordStaleCompletion() noexcept
	{
		++m_Statistics.m_StaleCompletionCount;
	}

	void AssetResidencyController::RecordAcceptedStateEvent(
		bool completedOperation) noexcept
	{
		++m_Statistics.m_AcceptedStateEventCount;
		m_Statistics.m_CompletedStateEventCount += completedOperation ? 1 : 0;
	}

	void AssetResidencyController::RecordStaleStateEvent() noexcept
	{
		++m_Statistics.m_StaleStateEventCount;
	}

	void AssetResidencyController::EndFrame() noexcept
	{
		m_Statistics.m_LastFrameReloadRequestCount =
			std::exchange(m_CurrentFrameReloadRequestCount, 0);
		m_Statistics.m_ReloadRequestHighWatermark = std::max(
			m_Statistics.m_ReloadRequestHighWatermark,
			m_Statistics.m_LastFrameReloadRequestCount);
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
			entry.m_Stamp.m_ResidencyOperationSerial == 0 &&
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
