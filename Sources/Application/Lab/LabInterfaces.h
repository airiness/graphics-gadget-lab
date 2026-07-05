#pragma once
#include "Application/Lab/LabSnapshot.h"

namespace gglab
{
	class ILabControl
	{
	public:
		virtual ~ILabControl() = default;

		virtual void RequestSwitchLab(const LabId& id) noexcept = 0;
		virtual void RequestSetParameter(
			const LabParameterId& id,
			const LabValue& value) noexcept = 0;
		virtual void RequestResetParameters() noexcept = 0;
		virtual void RequestRebuildScene() noexcept = 0;
		virtual void RequestRestartSession() noexcept = 0;
	};

	class ILabSnapshotSource
	{
	public:
		virtual ~ILabSnapshotSource() = default;
		virtual LabSnapshot GetLabSnapshot() const noexcept = 0;
	};
}
