#pragma once
#include "Core/CoreMacros.h"

#include <filesystem>
#include <mutex>
#include <unordered_map>

namespace gglab
{
	struct LocalDerivedDataCatalogSnapshot
	{
		uint64_t m_StoredBytes = 0;
		uint64_t m_StoredEntryCount = 0;
		uint64_t m_LastReconciledAtUnixMilliseconds = 0;
		uint64_t m_ReconciliationCount = 0;
		uint64_t m_ReconciliationFailureCount = 0;
		bool m_IsApproximate = true;
	};

	class LocalDerivedDataCatalog final
	{
	public:
		explicit LocalDerivedDataCatalog(std::filesystem::path rootDirectory) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(LocalDerivedDataCatalog);
		~LocalDerivedDataCatalog() = default;

		// Rebuilds the diagnostic view from filesystem metadata. Entry payloads are
		// deliberately not opened or validated by catalog reconciliation.
		[[nodiscard]] bool Reconcile() noexcept;
		void RecordEntry(const std::filesystem::path& path, uint64_t bytes) noexcept;
		void RemoveEntry(const std::filesystem::path& path) noexcept;
		void Clear() noexcept;
		[[nodiscard]] LocalDerivedDataCatalogSnapshot GetSnapshot() const noexcept;

	private:
		std::filesystem::path m_RootDirectory;
		mutable std::mutex m_Mutex;
		std::unordered_map<std::filesystem::path, uint64_t> m_Entries;
		uint64_t m_StoredBytes = 0;
		uint64_t m_LastReconciledAtUnixMilliseconds = 0;
		uint64_t m_ReconciliationCount = 0;
		uint64_t m_ReconciliationFailureCount = 0;
	};
}
