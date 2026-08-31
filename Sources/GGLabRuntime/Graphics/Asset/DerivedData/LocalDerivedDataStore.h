#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "Graphics/Asset/DerivedData/LocalDerivedDataCatalog.h"
#include "Graphics/Asset/DerivedData/LocalDerivedDataPlatform.h"

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
		LocalDerivedDataStore(std::filesystem::path rootDirectory,
			std::unique_ptr<LocalDerivedDataPlatformBase> platform) noexcept;
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
		[[nodiscard]] std::unique_ptr<LocalDerivedDataMaintenanceLockGuardBase>
			AcquireMaintenanceLock() noexcept;
		void CleanupOrphanTemporaryFilesLocked() noexcept;
		[[nodiscard]] std::vector<std::filesystem::path> CollectTrashPathsLocked() const noexcept;
		[[nodiscard]] std::filesystem::path MakeUniqueSiblingPath(
			const std::filesystem::path& basePath, std::string_view marker) noexcept;
		[[nodiscard]] std::filesystem::path MakeTrashPath() noexcept;
		static void ScheduleTrashCleanup(std::vector<std::filesystem::path> trashPaths) noexcept;

		std::unique_ptr<LocalDerivedDataPlatformBase> m_Platform;
		LocalDerivedDataRootIdentity m_RootIdentity;
		std::filesystem::path m_RootDirectory;
		LocalDerivedDataCatalog m_Catalog;
		std::unique_ptr<LocalDerivedDataMaintenanceLockBase> m_MaintenanceLock;
		mutable std::mutex m_Mutex;
		std::atomic_uint64_t m_HitCount = 0;
		std::atomic_uint64_t m_MissCount = 0;
		std::atomic_uint64_t m_CorruptionCount = 0;
		std::atomic_uint64_t m_ReadBytes = 0;
		std::atomic_uint64_t m_WriteCount = 0;
		std::atomic_uint64_t m_WriteFailureCount = 0;
		std::atomic_uint64_t m_WrittenBytes = 0;
	};
}
