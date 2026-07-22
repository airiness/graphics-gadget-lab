#pragma once
#include "Core/CoreMacros.h"
#include "Core/Platform/Win/Win32NamedMutex.h"

#include <filesystem>
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
			return !m_CanonicalRoot.empty() && !m_CanonicalUtf8.empty() &&
				!m_MutexName.empty();
		}
	};

	[[nodiscard]] LocalDerivedDataRootIdentity ResolveLocalDerivedDataRootIdentity(
		const std::filesystem::path& rootDirectory) noexcept;

	class LocalDerivedDataMaintenanceLock final
	{
	public:
		explicit LocalDerivedDataMaintenanceLock(
			const LocalDerivedDataRootIdentity& identity) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(LocalDerivedDataMaintenanceLock);
		~LocalDerivedDataMaintenanceLock() = default;

		[[nodiscard]] bool IsValid() const noexcept { return m_Mutex.IsValid(); }
		[[nodiscard]] win32::NamedMutexGuard Acquire() const noexcept
		{
			return m_Mutex.Acquire();
		}

	private:
		win32::NamedMutex m_Mutex;
	};
}
