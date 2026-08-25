#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"

#include <filesystem>
#include <memory>
#include <string>

namespace gglab
{
	// Platform identity is opaque to portable DDC code. It follows the target
	// filesystem's canonicalization and case-sensitivity rules.
	struct LocalDerivedDataRootIdentity final
	{
		std::filesystem::path m_CanonicalRoot;
		std::string m_PlatformIdentity;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return !m_CanonicalRoot.empty() && !m_PlatformIdentity.empty();
		}
	};

	class LocalDerivedDataMaintenanceLockGuardBase
	{
	public:
		GGLAB_DELETE_COPYABLE_MOVABLE(LocalDerivedDataMaintenanceLockGuardBase);
		virtual ~LocalDerivedDataMaintenanceLockGuardBase() = default;

		[[nodiscard]] virtual bool IsAcquired() const noexcept = 0;
		[[nodiscard]] virtual bool WasAbandoned() const noexcept = 0;

	protected:
		LocalDerivedDataMaintenanceLockGuardBase() noexcept = default;
	};

	class LocalDerivedDataMaintenanceLockBase
	{
	public:
		GGLAB_DELETE_COPYABLE_MOVABLE(LocalDerivedDataMaintenanceLockBase);
		virtual ~LocalDerivedDataMaintenanceLockBase() = default;

		[[nodiscard]] virtual bool IsValid() const noexcept = 0;
		[[nodiscard]] virtual std::unique_ptr<LocalDerivedDataMaintenanceLockGuardBase>
			Acquire() const noexcept = 0;

	protected:
		LocalDerivedDataMaintenanceLockBase() noexcept = default;
	};

	class LocalDerivedDataPlatformBase
	{
	public:
		GGLAB_DELETE_COPYABLE_MOVABLE(LocalDerivedDataPlatformBase);
		virtual ~LocalDerivedDataPlatformBase() = default;

		[[nodiscard]] virtual LocalDerivedDataRootIdentity ResolveRootIdentity(
			const std::filesystem::path& rootDirectory) const noexcept = 0;
		[[nodiscard]] virtual std::unique_ptr<LocalDerivedDataMaintenanceLockBase>
			CreateMaintenanceLock(
				const LocalDerivedDataRootIdentity& identity) const noexcept = 0;
		// The returned filesystem-safe token must be collision-resistant across
		// processes using the same DDC deployment.
		[[nodiscard]] virtual std::string CreateUniquePathToken() noexcept = 0;

	protected:
		LocalDerivedDataPlatformBase() noexcept = default;
	};

	// Each target supplies exactly one default leaf. A target must provide a real
	// process-shared lock unless its single-process DDC restriction is explicit.
	[[nodiscard]] std::unique_ptr<LocalDerivedDataPlatformBase>
		CreateDefaultLocalDerivedDataPlatform() noexcept;
}
