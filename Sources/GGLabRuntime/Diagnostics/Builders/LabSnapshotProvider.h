#pragma once
#include "Diagnostics/SnapshotProvider.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"

#include <functional>
#include <utility>

namespace gglab
{
	class LabSnapshotProvider final : public SnapshotProviderBase
	{
	public:
		using SourceResolver = std::function<const LabSnapshotSourceBase*()>;

		explicit LabSnapshotProvider(SourceResolver sourceResolver) noexcept :
			m_SourceResolver(std::move(sourceResolver))
		{
		}

		[[nodiscard]] SnapshotId GetId() const noexcept override;
		[[nodiscard]] std::string_view GetName() const noexcept override { return "Lab"; }
		void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override;

	private:
		SourceResolver m_SourceResolver;
	};
}
