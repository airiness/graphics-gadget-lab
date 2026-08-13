#pragma once
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "DevTools/EnumText/EnumText.h"

namespace gglab::devtools
{
	template <> struct EnumTextTraits<LabSnapshotRunState>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{LabSnapshotRunState::Uninitialized, "Uninitialized"},
			EnumTextEntry{LabSnapshotRunState::Loading, "Loading"},
			EnumTextEntry{LabSnapshotRunState::WarmingUp, "Warming Up"},
			EnumTextEntry{LabSnapshotRunState::Ready, "Ready"},
			EnumTextEntry{LabSnapshotRunState::Capturing, "Capturing"},
			EnumTextEntry{LabSnapshotRunState::Completed, "Completed"},
			EnumTextEntry{LabSnapshotRunState::Failed, "Failed"},
		};
	};
}
