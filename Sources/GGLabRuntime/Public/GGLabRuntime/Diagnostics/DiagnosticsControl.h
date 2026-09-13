#pragma once

#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"

namespace gglab
{
	// Non-owning command seam for diagnostics capture policy. Commands affect
	// Runtime-owned publication state; capture executes only inside an active frame.
	class DiagnosticsControl
	{
	public:
		virtual ~DiagnosticsControl() = default;

		template <typename T> void RequestRefresh() noexcept
		{
			RequestRefresh(SnapshotIdOf<T>);
		}

		virtual void RequestRefresh(SnapshotId id) noexcept = 0;
	};
}
