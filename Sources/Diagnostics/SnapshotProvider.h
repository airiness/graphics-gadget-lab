#pragma once

#include "Diagnostics/SnapshotCommon.h"

namespace gglab
{
	struct SnapshotContext;
	class SnapshotStore;

	class SnapshotProviderBase
	{
	public:
		virtual ~SnapshotProviderBase() = default;

		[[nodiscard]] virtual SnapshotId GetId() const noexcept = 0;
		[[nodiscard]] virtual std::string_view GetName() const noexcept = 0;
		virtual void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept = 0;
	};
}
