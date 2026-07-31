#pragma once
#include "Diagnostics/SnapshotProvider.h"

namespace gglab
{
	class LabRuntimeLocatorBase;

	class LabSnapshotProvider final : public SnapshotProviderBase
	{
	public:
		explicit LabSnapshotProvider(const LabRuntimeLocatorBase* runtimeLocator) noexcept :
			m_RuntimeLocator(runtimeLocator)
		{
		}

		[[nodiscard]] SnapshotId GetId() const noexcept override;
		[[nodiscard]] std::string_view GetName() const noexcept override { return "Lab"; }
		void Capture(const SnapshotContext& context, SnapshotStore& store) noexcept override;

	private:
		const LabRuntimeLocatorBase* m_RuntimeLocator = nullptr;
	};
}
