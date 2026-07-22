#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"

#include <atomic>
#include <filesystem>
#include <limits>
#include <mutex>
#include <span>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace gglab
{
	enum class DerivedDataReadDisposition : uint8_t
	{
		Miss,
		Hit,
		Corrupt,
	};

	struct DerivedDataReadResult
	{
		DerivedDataReadDisposition m_Disposition = DerivedDataReadDisposition::Miss;
		ArtifactContentDigest m_ArtifactContentDigest{};
		std::vector<std::byte> m_Payload;
	};

	struct LocalDerivedDataReadOptions
	{
		uint64_t m_MaxContainerBytes = std::numeric_limits<uint64_t>::max();
	};

	[[nodiscard]] uint64_t ComputeLocalDerivedDataContainerByteLimit(
		std::string_view artifactType,
		uint64_t maximumPayloadBytes) noexcept;

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
	};

	class LocalDerivedDataStore final
	{
	public:
		explicit LocalDerivedDataStore(std::filesystem::path rootDirectory = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(LocalDerivedDataStore);
		~LocalDerivedDataStore() = default;

		[[nodiscard]] DerivedDataReadResult Read(
			const DerivedDataKey& key,
			std::string_view artifactType,
			uint32_t schemaVersion,
			LocalDerivedDataReadOptions options = {}) noexcept;
		[[nodiscard]] bool Write(
			const DerivedDataKey& key,
			std::string_view artifactType,
			uint32_t schemaVersion,
			const ArtifactContentDigest& artifactContentDigest,
			std::span<const std::byte> payload) noexcept;
		[[nodiscard]] bool Contains(const DerivedDataKey& key) const noexcept;
		void DiscardCorrupt(const DerivedDataKey& key) noexcept;
		void Clear() noexcept;
		[[nodiscard]] LocalDerivedDataStoreStatistics GetStatistics() const noexcept;
		[[nodiscard]] bool IsEnabled() const noexcept { return !m_RootDirectory.empty(); }

	private:
		[[nodiscard]] std::filesystem::path EntryPath(const DerivedDataKey& key) const;
		void RefreshStoredStatistics() noexcept;

		std::filesystem::path m_RootDirectory;
		mutable std::mutex m_Mutex;
		std::unordered_set<std::filesystem::path> m_EntryPaths;
		std::atomic_uint64_t m_TemporarySerial = 1;
		std::atomic_uint64_t m_StoredBytes = 0;
		std::atomic_uint64_t m_StoredEntryCount = 0;
		std::atomic_uint64_t m_HitCount = 0;
		std::atomic_uint64_t m_MissCount = 0;
		std::atomic_uint64_t m_CorruptionCount = 0;
		std::atomic_uint64_t m_ReadBytes = 0;
		std::atomic_uint64_t m_WriteCount = 0;
		std::atomic_uint64_t m_WriteFailureCount = 0;
		std::atomic_uint64_t m_WrittenBytes = 0;
	};
}
