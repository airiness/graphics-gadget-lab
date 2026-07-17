#pragma once
#include "Graphics/Asset/Residency/AssetResidencyTypes.h"

namespace gglab
{
	class AssetResidencyController final
	{
	public:
		[[nodiscard]] AssetResidencyPlan BuildPlan(
			const AssetResidencyInventorySnapshot& snapshot,
			const AssetResidencyConfig& config) const noexcept;

		[[nodiscard]] static bool IsEvictionCandidate(
			const AssetResidencyInventoryEntry& entry,
			uint64_t snapshotFrame,
			const AssetResidencyConfig& config) noexcept;
	};
}
