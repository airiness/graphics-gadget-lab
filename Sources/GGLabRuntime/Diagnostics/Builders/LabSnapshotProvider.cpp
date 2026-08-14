#include "Diagnostics/Builders/LabSnapshotProvider.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Diagnostics/SnapshotStore.h"

namespace gglab
{
	SnapshotId LabSnapshotProvider::GetId() const noexcept
	{
		return SnapshotIdOf<LabSnapshot>;
	}

	void LabSnapshotProvider::Capture(const SnapshotContext& context, SnapshotStore& store) noexcept
	{
		GGLAB_UNUSED(context);
		auto& snapshot = store.GetOrCreate<LabSnapshot>();
		const LabSnapshotSourceBase* source = m_SourceResolver ? m_SourceResolver() : nullptr;
		snapshot = source ? source->GetLabSnapshot() : LabSnapshot{};
	}
}
