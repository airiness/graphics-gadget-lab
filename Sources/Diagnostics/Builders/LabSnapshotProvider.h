#pragma once
#include "Diagnostics/SnapshotProvider.h"

namespace gglab
{
	class ILabSnapshotSource;

	class LabSnapshotProvider final : public SnapshotProviderBase
	{
	public:
		explicit LabSnapshotProvider(const ILabSnapshotSource* source) noexcept :
			m_Source(source)
		{}

		[[nodiscard]] SnapshotId GetId() const noexcept override;
		[[nodiscard]] std::string_view GetName() const noexcept override { return "Lab"; }
		void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override;

	private:
		const ILabSnapshotSource* m_Source = nullptr;
	};
}
