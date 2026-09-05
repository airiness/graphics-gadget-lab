#pragma once
#include "Diagnostics/SnapshotProvider.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"

namespace gglab
{
	class LabSnapshotProvider final : public SnapshotProviderBase
	{
	public:
		[[nodiscard]] SnapshotId GetId() const noexcept override;
		[[nodiscard]] std::string_view GetName() const noexcept override { return "Lab"; }
		void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override;
	};
}
