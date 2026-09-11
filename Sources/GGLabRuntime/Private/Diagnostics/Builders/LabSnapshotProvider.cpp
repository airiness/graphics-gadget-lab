#include "Diagnostics/Builders/LabSnapshotProvider.h"
#include "Diagnostics/SnapshotStore.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsSession.h"

namespace gglab
{
	SnapshotId LabSnapshotProvider::GetId() const noexcept
	{
		return SnapshotIdOf<LabSnapshot>;
	}

	void LabSnapshotProvider::Capture(
		const DiagnosticsFrameContext& context, SnapshotStore& store) noexcept
	{
		auto& snapshot = store.GetOrCreate<LabSnapshot>();
		const LabSnapshotSourceBase* source = context.m_LabSnapshotSource;
		snapshot = source ? source->GetLabSnapshot() : LabSnapshot{};
	}
}
