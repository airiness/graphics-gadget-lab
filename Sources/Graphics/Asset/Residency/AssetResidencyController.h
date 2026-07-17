#pragma once
#include "Graphics/Asset/Residency/AssetResidencyTypes.h"

namespace gglab
{
	class AssetResidencyController final
	{
	public:
		[[nodiscard]] AssetResidencyOperation BeginResidencyOperation(
			AssetLifecycle& lifecycle,
			AssetContentVersion contentVersion,
			AssetResidencyOperationKind kind) noexcept;

		[[nodiscard]] static bool IsCurrentOperation(
			const AssetLifecycle& lifecycle,
			const AssetResidencyOperation& operation) noexcept;

		static void CompleteResidencyOperation(
			AssetLifecycle& lifecycle,
			const AssetResidencyOperation& operation) noexcept;

		static void InvalidateResidencyOperation(AssetLifecycle& lifecycle) noexcept;

		[[nodiscard]] AssetResidencyPlan BuildPlan(
			const AssetResidencyInventorySnapshot& snapshot,
			const AssetResidencyConfig& config) const noexcept;

		[[nodiscard]] static bool IsEvictionCandidate(
			const AssetResidencyInventoryEntry& entry,
			uint64_t snapshotFrame,
			const AssetResidencyConfig& config) noexcept;

	private:
		uint64_t m_NextOperationSerial = 1;
	};
}
