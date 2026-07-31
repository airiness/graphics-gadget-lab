#pragma once
#include "Graphics/Asset/Residency/AssetResidencyTypes.h"

namespace gglab
{
	class AssetResidencyController final
	{
	public:
		void SetConfig(const AssetResidencyConfig& config) noexcept;
		[[nodiscard]] const AssetResidencyConfig& GetConfig() const noexcept;
		[[nodiscard]] AssetResidencyStatistics GetStatistics(uint64_t logicalResidentBytes,
			uint64_t pendingEvictionBytes, uint32_t pendingEvictionCount,
			uint32_t reloadingAssetCount) const noexcept;

		[[nodiscard]] AssetResidencyOperation BeginResidencyOperation(AssetLifecycle& lifecycle,
			AssetContentVersion contentVersion, AssetResidencyOperationKind kind) noexcept;

		[[nodiscard]] static bool IsCurrentOperation(
			const AssetLifecycle& lifecycle, const AssetResidencyOperation& operation) noexcept;
		[[nodiscard]] static bool IsCurrentOperation(
			const AssetLifecycle& lifecycle, const AssetOperationToken& operation) noexcept;

		static void CompleteResidencyOperation(
			AssetLifecycle& lifecycle, const AssetResidencyOperation& operation) noexcept;
		static void CompleteResidencyOperation(
			AssetLifecycle& lifecycle, const AssetOperationToken& operation) noexcept;

		static void InvalidateResidencyOperation(AssetLifecycle& lifecycle) noexcept;

		[[nodiscard]] AssetResidencyPlan BuildPlan(
			const AssetResidencyInventorySnapshot& snapshot) const noexcept;

		[[nodiscard]] bool StillEligible(const AssetResidencyAction& action,
			const AssetResidencyInventoryEntry& currentEntry, uint64_t currentFrame,
			uint64_t projectedResidentBytes) const noexcept;

		static void MarkAssetUsed(AssetLifecycle& lifecycle, uint64_t currentFrame) noexcept;

		[[nodiscard]] static bool SetResidencyPolicy(
			AssetLifecycle& lifecycle, AssetResidencyPolicy policy, bool isReserved) noexcept;

		void RecordPlan(const AssetResidencyPlan& plan) noexcept;
		void RecordEviction(bool cancelled, uint64_t residentBytes) noexcept;
		void RecordReloadRequest(bool coalesced) noexcept;
		void RecordReloadCoalesced() noexcept;
		void RecordRevalidationRejection() noexcept;
		void RecordStaleCompletion() noexcept;
		void RecordAcceptedStateEvent(bool completedOperation) noexcept;
		void RecordStaleStateEvent() noexcept;
		void EndFrame() noexcept;

		[[nodiscard]] static bool IsEvictionCandidate(const AssetResidencyInventoryEntry& entry,
			uint64_t snapshotFrame, const AssetResidencyConfig& config) noexcept;

	private:
		uint64_t m_NextOperationSerial = 1;
		AssetResidencyStatistics m_Statistics{};
		uint32_t m_CurrentFrameReloadRequestCount = 0;
	};
}
