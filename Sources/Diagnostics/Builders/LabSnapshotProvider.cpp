#include "Core/Precompiled.h"
#include "Diagnostics/Builders/LabSnapshotProvider.h"
#include "Application/Lab/LabInterfaces.h"
#include "Diagnostics/SnapshotStore.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"

namespace gglab
{
	SnapshotId LabSnapshotProvider::GetId() const noexcept
	{
		return SnapshotIdOf<LabSnapshot>;
	}

	void LabSnapshotProvider::Capture(
		const SnapshotContext& context,
		SnapshotStore& store) noexcept
	{
		GGLAB_UNUSED(context);
		auto& snapshot = store.GetOrCreate<LabSnapshot>();
		snapshot = m_Source ? m_Source->GetLabSnapshot() : LabSnapshot{};
	}
}
