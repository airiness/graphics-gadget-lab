#pragma once
#include "Application/Lab/LabRunConfig.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"

namespace gglab
{
	class LabRuntime;

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
		virtual void RequestRunConfig(const LabRunConfig& config) noexcept = 0;
	};

	class ILabSnapshotSource
	{
	public:
		virtual ~ILabSnapshotSource() = default;
		virtual LabSnapshot GetLabSnapshot() const noexcept = 0;
	};

	class LabRuntimeLocatorBase
	{
	public:
		virtual ~LabRuntimeLocatorBase() = default;
		virtual LabRuntime* GetLabRuntimeIfCreated() noexcept = 0;
		virtual const LabRuntime* GetLabRuntimeIfCreated() const noexcept = 0;
	};
}
