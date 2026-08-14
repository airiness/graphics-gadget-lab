#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"

#include <filesystem>
#include <memory>
#include <string>

namespace gglab
{
	// Existing junctions and symlinks are resolved before the identity is folded to
	// invariant lowercase UTF-8. The lock coordinates processes in one Windows session.
	struct LocalDerivedDataRootIdentity
	{
		std::filesystem::path m_CanonicalRoot;
		std::string m_CanonicalUtf8;
		std::wstring m_MutexName;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return !m_CanonicalRoot.empty() && !m_CanonicalUtf8.empty() && !m_MutexName.empty();
		}
	};

	[[nodiscard]] LocalDerivedDataRootIdentity ResolveLocalDerivedDataRootIdentity(
		const std::filesystem::path& rootDirectory) noexcept;

	class LocalDerivedDataMaintenanceLockGuard final
	{
	public:
		LocalDerivedDataMaintenanceLockGuard() noexcept;
		LocalDerivedDataMaintenanceLockGuard(LocalDerivedDataMaintenanceLockGuard&& other) noexcept;
		LocalDerivedDataMaintenanceLockGuard& operator=(LocalDerivedDataMaintenanceLockGuard&& other) noexcept;
		GGLAB_DELETE_COPYABLE(LocalDerivedDataMaintenanceLockGuard);
		~LocalDerivedDataMaintenanceLockGuard();

		[[nodiscard]] bool IsAcquired() const noexcept;
		[[nodiscard]] bool WasAbandoned() const noexcept;

	private:
		friend class LocalDerivedDataMaintenanceLock;
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};

	class LocalDerivedDataMaintenanceLock final
	{
	public:
		explicit LocalDerivedDataMaintenanceLock(const LocalDerivedDataRootIdentity& identity) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(LocalDerivedDataMaintenanceLock);
		~LocalDerivedDataMaintenanceLock();

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] LocalDerivedDataMaintenanceLockGuard Acquire() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
