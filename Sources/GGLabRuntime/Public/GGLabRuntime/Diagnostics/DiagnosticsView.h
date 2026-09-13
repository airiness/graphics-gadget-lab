#pragma once

#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"

#include <vector>

namespace gglab
{
	// Non-owning read-only seam for immutable Runtime diagnostics. Provider
	// registration, capture requests and live capture context stay outside this view.
	class DiagnosticsView
	{
	public:
		virtual ~DiagnosticsView() = default;

		template <typename T> [[nodiscard]] const T* GetSnapshot() noexcept
		{
			return static_cast<const T*>(GetSnapshotData(SnapshotIdOf<T>));
		}

		[[nodiscard]] virtual std::vector<SnapshotProfile> GetProfiles() const = 0;

	private:
		[[nodiscard]] virtual const void* GetSnapshotData(SnapshotId id) noexcept = 0;
	};
}
