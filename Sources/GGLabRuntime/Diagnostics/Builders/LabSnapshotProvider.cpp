#include "Diagnostics/Builders/LabSnapshotProvider.h"
#include "Diagnostics/SnapshotContext.h"
#include "Diagnostics/SnapshotStore.h"

namespace gglab
{
	SnapshotId LabSnapshotProvider::GetId() const noexcept
	{
		return SnapshotIdOf<LabSnapshot>;
	}

	void LabSnapshotProvider::Capture(const SnapshotContext& context, SnapshotStore& store) noexcept
	{
		auto& snapshot = store.GetOrCreate<LabSnapshot>();
		const LabSnapshotSourceBase* source = context.m_LabSnapshotSource;
		snapshot = source ? source->GetLabSnapshot() : LabSnapshot{};
	}
}
