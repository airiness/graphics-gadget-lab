#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "Graphics/Asset/DerivedData/LocalDerivedDataCatalog.h"
#include "Graphics/Asset/DerivedData/LocalDerivedDataMaintenanceLock.h"

#include <atomic>
#include <filesystem>
#include <limits>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

namespace gglab
{
	enum class DerivedDataReadDisposition : uint8_t
	{
		Miss,
		Hit,
		Corrupt,
	};

	enum class DerivedDataPresence : uint8_t
	{
		Missing,
		Present,
		Inaccessible,
	};

	struct DerivedDataReadResult
	{
		DerivedDataReadDisposition m_Disposition = DerivedDataReadDisposition::Miss;
		ArtifactContentDigest m_ArtifactContentDigest{};
		Sha256Digest m_PayloadDigest{};
		std::vector<std::byte> m_Payload;
	};

	struct LocalDerivedDataReadOptions
	{
		uint64_t m_MaxContainerBytes = std::numeric_limits<uint64_t>::max();
	};

	[[nodiscard]] uint64_t ComputeLocalDerivedDataContainerByteLimit(
		std::string_view artifactType, uint64_t maximumPayloadBytes) noexcept;

	struct LocalDerivedDataStoreStatistics
	{
		uint64_t m_StoredBytes = 0;
		uint64_t m_StoredEntryCount = 0;
		uint64_t m_HitCount = 0;
		uint64_t m_MissCount = 0;
		uint64_t m_CorruptionCount = 0;
		uint64_t m_ReadBytes = 0;
		uint64_t m_WriteCount = 0;
		uint64_t m_WriteFailureCount = 0;
		uint64_t m_WrittenBytes = 0;
		uint64_t m_CatalogLastReconciledAtUnixMilliseconds = 0;
		uint64_t m_CatalogReconciliationCount = 0;
		uint64_t m_CatalogReconciliationFailureCount = 0;
		bool m_IsCatalogApproximate = true;
	};

	class LocalDerivedDataStore final
	{
	public:
		explicit LocalDerivedDataStore(std::filesystem::path rootDirectory = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(LocalDerivedDataStore);
		~LocalDerivedDataStore() = default;

		[[nodiscard]] DerivedDataReadResult Read(const DerivedDataKey& key,
			std::string_view artifactType, uint32_t schemaVersion,
			LocalDerivedDataReadOptions options = {}) noexcept;
		[[nodiscard]] bool Write(const DerivedDataKey& key, std::string_view artifactType,
			uint32_t schemaVersion, const ArtifactContentDigest& artifactContentDigest,
			std::span<const std::byte> payload) noexcept;
		// Present means that the entry path exists; Read performs container validation.
		[[nodiscard]] DerivedDataPresence Probe(const DerivedDataKey& key) const noexcept;
		[[nodiscard]] bool Contains(const DerivedDataKey& key) const noexcept
		{
			return Probe(key) == DerivedDataPresence::Present;
		}
		void DiscardObservedCorrupt(const DerivedDataKey& key, std::string_view artifactType,
			uint32_t schemaVersion, const ArtifactContentDigest& observedArtifactContentDigest,
			const Sha256Digest& observedPayloadDigest,
			LocalDerivedDataReadOptions options = {}) noexcept;
		[[nodiscard]] bool Clear() noexcept;
		[[nodiscard]] bool ReconcileCatalog() noexcept;
		[[nodiscard]] LocalDerivedDataStoreStatistics GetStatistics() const noexcept;
		[[nodiscard]] bool IsEnabled() const noexcept { return !m_RootDirectory.empty(); }

	private:
		[[nodiscard]] std::filesystem::path EntryPath(const DerivedDataKey& key) const;
		[[nodiscard]] LocalDerivedDataMaintenanceLockGuard AcquireMaintenanceLock() noexcept;
		void CleanupOrphanTemporaryFilesLocked() noexcept;
		[[nodiscard]] std::vector<std::filesystem::path> CollectTrashPathsLocked() const noexcept;
		[[nodiscard]] std::filesystem::path MakeTrashPath() noexcept;
		static void ScheduleTrashCleanup(std::vector<std::filesystem::path> trashPaths) noexcept;

		LocalDerivedDataRootIdentity m_RootIdentity;
		std::filesystem::path m_RootDirectory;
		LocalDerivedDataCatalog m_Catalog;
		LocalDerivedDataMaintenanceLock m_MaintenanceLock;
		mutable std::mutex m_Mutex;
		std::atomic_uint64_t m_TemporarySerial = 1;
		std::atomic_uint64_t m_TrashSerial = 1;
		std::atomic_uint64_t m_HitCount = 0;
		std::atomic_uint64_t m_MissCount = 0;
		std::atomic_uint64_t m_CorruptionCount = 0;
		std::atomic_uint64_t m_ReadBytes = 0;
		std::atomic_uint64_t m_WriteCount = 0;
		std::atomic_uint64_t m_WriteFailureCount = 0;
		std::atomic_uint64_t m_WrittenBytes = 0;
	};
}
