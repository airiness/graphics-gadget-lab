#pragma once

#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"

#include <vector>

namespace gglab
{
	// Non-owning read/request seam for immutable Runtime diagnostics. Consumers may
	// request capture, but provider registration and live capture context stay private.
	class DiagnosticsView
	{
	public:
		virtual ~DiagnosticsView() = default;

		template <typename T> [[nodiscard]] const T* GetSnapshot() noexcept
		{
			return static_cast<const T*>(GetSnapshotData(SnapshotIdOf<T>));
		}

		template <typename T> void RequestRefresh() noexcept
		{
			RequestRefresh(SnapshotIdOf<T>);
		}

		virtual void RequestRefresh(SnapshotId id) noexcept = 0;
		[[nodiscard]] virtual std::vector<SnapshotProfile> GetProfiles() const = 0;

	private:
		[[nodiscard]] virtual const void* GetSnapshotData(SnapshotId id) noexcept = 0;
	};
}
