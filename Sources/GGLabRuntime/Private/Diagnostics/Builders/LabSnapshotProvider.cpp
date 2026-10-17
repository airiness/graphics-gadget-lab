#include "Diagnostics/Builders/LabSnapshotProvider.h"
#include "Diagnostics/SnapshotStore.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsSession.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"

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
		snapshot = source ?
			source->GetLabSnapshot({
			.m_RequestedTemporalAASettings = context.m_DisplayViewSettings ?
				&context.m_DisplayViewSettings->m_TemporalAA :
				nullptr,
			.m_TemporalFramePlan = context.m_TemporalFramePlan,
			.m_FrameSerial = context.m_FrameSerial, }) :
			LabSnapshot{};
	}
}
