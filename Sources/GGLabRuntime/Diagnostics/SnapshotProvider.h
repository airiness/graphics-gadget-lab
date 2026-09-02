#pragma once

#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"

#include <string_view>

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

	template <typename Snapshot> class TypedSnapshotProviderBase : public SnapshotProviderBase
	{
	public:
		[[nodiscard]] SnapshotId GetId() const noexcept final { return SnapshotIdOf<Snapshot>; }
	};
}
