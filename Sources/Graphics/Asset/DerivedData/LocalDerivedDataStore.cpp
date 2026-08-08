#include "Core/Precompiled.h"
#include "Graphics/Asset/DerivedData/LocalDerivedDataStore.h"
#include "Core/Hash/Sha256.h"
#include "Core/Log/Logger.h"
#include "Core/Utility/PathUtils.h"

#include <thread>

namespace gglab
{
	namespace
	{
		constexpr std::array<std::byte, 8> ContainerMagic{
			std::byte{'G'},
			std::byte{'G'},
			std::byte{'L'},
			std::byte{'A'},
			std::byte{'B'},
			std::byte{'D'},
			std::byte{'D'},
			std::byte{'C'},
		};
		constexpr uint32_t ContainerVersion = 1;
		constexpr uint64_t ContainerFixedBytes =
			ContainerMagic.size() + 3 * sizeof(uint32_t) + sizeof(uint64_t) +
			DerivedDataKey{}.m_Value.size() + ArtifactContentDigest{}.m_Value.size() +
			Sha256Hash{}.m_Value.size();

		class BinaryWriter
		{
		public:
			void AddBytes(std::span<const std::byte> bytes)
			{
				m_Bytes.insert(m_Bytes.end(), bytes.begin(), bytes.end());
			}
			void AddU32(uint32_t value)
			{
				for (uint32_t i = 0; i < 4; ++i)
					m_Bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xffu));
			}
			void AddU64(uint64_t value)
			{
				for (uint32_t i = 0; i < 8; ++i)
					m_Bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xffu));
			}
			std::vector<std::byte> m_Bytes;
		};

		class BinaryReader
		{
		public:
			explicit BinaryReader(std::span<const std::byte> bytes) noexcept : m_Bytes(bytes) {}
			bool ReadBytes(std::span<std::byte> output) noexcept
			{
				if (output.size() > m_Bytes.size() - m_Offset)
					return false;
				std::memcpy(output.data(), m_Bytes.data() + m_Offset, output.size());
				m_Offset += output.size();
				return true;
			}
			bool ReadU32(uint32_t& value) noexcept
			{
				uint64_t decoded = 0;
				if (!ReadUnsigned(decoded, 4))
					return false;
				value = static_cast<uint32_t>(decoded);
				return true;
			}
			bool ReadU64(uint64_t& value) noexcept { return ReadUnsigned(value, 8); }
			[[nodiscard]] size_t Offset() const noexcept { return m_Offset; }

		private:
			bool ReadUnsigned(uint64_t& value, size_t width) noexcept
			{
				if (width > m_Bytes.size() - m_Offset)
					return false;
				value = 0;
				for (size_t i = 0; i < width; ++i)
					value |= static_cast<uint64_t>(std::to_integer<uint8_t>(m_Bytes[m_Offset + i]))
					<< (i * 8);
				m_Offset += width;
				return true;
			}
			std::span<const std::byte> m_Bytes;
			size_t m_Offset = 0;
		};

		[[nodiscard]] DerivedDataReadResult ReadEntry(const std::filesystem::path& path,
			const DerivedDataKey& key, std::string_view artifactType, uint32_t schemaVersion,
			LocalDerivedDataReadOptions options) noexcept
		{
			DerivedDataReadResult result{};
			std::ifstream stream(path, std::ios::binary | std::ios::ate);
			if (!stream)
			{
				return result;
			}

			const std::streamoff end = stream.tellg();
			if (end <= 0 || static_cast<uint64_t>(end) > options.m_MaxContainerBytes ||
				static_cast<uint64_t>(end) > std::numeric_limits<size_t>::max())
			{
				result.m_Disposition = DerivedDataReadDisposition::Corrupt;
				return result;
			}

			std::vector<std::byte> fileBytes(static_cast<size_t>(end));
			stream.seekg(0, std::ios::beg);
			stream.read(reinterpret_cast<char*>(fileBytes.data()),
				static_cast<std::streamsize>(fileBytes.size()));
			BinaryReader reader(fileBytes);
			std::array<std::byte, 8> magic{};
			uint32_t containerVersion = 0;
			uint32_t storedSchema = 0;
			uint32_t typeBytes = 0;
			uint64_t payloadBytes = 0;
			DerivedDataKey storedKey{};
			ArtifactContentDigest storedArtifactDigest{};
			Sha256Hash storedPayloadDigest{};
			const bool headerValid =
				stream &&
				reader.ReadBytes(magic) && reader.ReadU32(containerVersion) &&
				reader.ReadU32(storedSchema) &&
				reader.ReadU32(typeBytes) && reader.ReadU64(payloadBytes) &&
				reader.ReadBytes(storedKey.m_Value) &&
				reader.ReadBytes(storedArtifactDigest.m_Value) &&
				reader.ReadBytes(storedPayloadDigest.m_Value);
			const uint64_t headerBytes = reader.Offset();
			const bool sizesValid = headerBytes <= fileBytes.size() &&
				typeBytes <= fileBytes.size() - headerBytes &&
				payloadBytes <= fileBytes.size() - headerBytes - typeBytes;
			if (headerValid &&
				magic == ContainerMagic && containerVersion == ContainerVersion &&
				storedSchema == schemaVersion && storedKey == key &&
				typeBytes == artifactType.size() && sizesValid &&
				headerBytes + typeBytes + payloadBytes == fileBytes.size())
			{
				const auto storedType = std::span(fileBytes).subspan(reader.Offset(), typeBytes);
				const auto expectedType =
					std::as_bytes(std::span{ artifactType.data(), artifactType.size() });
				const auto payload = std::span(fileBytes).subspan(
					reader.Offset() + typeBytes, static_cast<size_t>(payloadBytes));
				const Sha256Hash payloadDigest = ComputeSha256(payload);
				if (std::ranges::equal(storedType, expectedType) &&
					payloadDigest.IsValid() && payloadDigest.m_Value == storedPayloadDigest.m_Value &&
					storedArtifactDigest.IsValid())
				{
					result.m_Disposition = DerivedDataReadDisposition::Hit;
					result.m_ArtifactContentDigest = storedArtifactDigest;
					result.m_PayloadDigest = payloadDigest;
					result.m_Payload.assign(payload.begin(), payload.end());
				}
			}
			if (result.m_Disposition != DerivedDataReadDisposition::Hit)
			{
				result.m_Disposition = DerivedDataReadDisposition::Corrupt;
			}
			return result;
		}
	}

	uint64_t ComputeLocalDerivedDataContainerByteLimit(
		std::string_view artifactType, uint64_t maximumPayloadBytes) noexcept
	{
		constexpr uint64_t MaxValue = std::numeric_limits<uint64_t>::max();
		if (artifactType.size() > MaxValue - ContainerFixedBytes ||
			maximumPayloadBytes > MaxValue - ContainerFixedBytes -
				static_cast<uint64_t>(artifactType.size()))
		{
			return MaxValue;
		}
		return ContainerFixedBytes + static_cast<uint64_t>(artifactType.size()) +
			maximumPayloadBytes;
	}

	LocalDerivedDataStore::LocalDerivedDataStore(std::filesystem::path rootDirectory) noexcept :
		m_RootIdentity(ResolveLocalDerivedDataRootIdentity(rootDirectory)),
		m_RootDirectory(m_RootIdentity.m_CanonicalRoot), m_Catalog(m_RootDirectory),
		m_MaintenanceLock(m_RootIdentity)
	{
		if (IsEnabled())
		{
			if (!utils::CreateDirectoryIfNotExist(m_RootDirectory))
				return;
			std::vector<std::filesystem::path> trashPaths;
			{
				win32::NamedMutexGuard maintenance = AcquireMaintenanceLock();
				if (maintenance.IsAcquired())
				{
					CleanupOrphanTemporaryFilesLocked();
					trashPaths = CollectTrashPathsLocked();
				}
			}
			ScheduleTrashCleanup(std::move(trashPaths));
			GGLAB_UNUSED(m_Catalog.Reconcile());
		}
	}

	DerivedDataReadResult LocalDerivedDataStore::Read(const DerivedDataKey& key,
		std::string_view artifactType, uint32_t schemaVersion,
		LocalDerivedDataReadOptions options) noexcept
	{
		if (!IsEnabled() || !key.IsValid())
		{
			m_MissCount.fetch_add(1, std::memory_order_relaxed);
			return {};
		}
		const std::filesystem::path path = EntryPath(key);
		DerivedDataReadResult result = ReadEntry(path, key, artifactType, schemaVersion, options);
		if (result.m_Disposition == DerivedDataReadDisposition::Miss)
		{
			m_Catalog.RemoveEntry(path);
			m_MissCount.fetch_add(1, std::memory_order_relaxed);
			return result;
		}

		if (result.m_Disposition == DerivedDataReadDisposition::Hit)
		{
			std::error_code errorCode;
			const uint64_t bytes = std::filesystem::file_size(path, errorCode);
			if (!errorCode)
			{
				m_Catalog.RecordEntry(path, bytes);
			}
			m_HitCount.fetch_add(1, std::memory_order_relaxed);
			m_ReadBytes.fetch_add(result.m_Payload.size(), std::memory_order_relaxed);
			return result;
		}
		std::scoped_lock lock(m_Mutex);
		win32::NamedMutexGuard maintenance = AcquireMaintenanceLock();
		if (!maintenance.IsAcquired())
		{
			m_CorruptionCount.fetch_add(1, std::memory_order_relaxed);
			return result;
		}
		// Clear may replace the root while the lock-free read is in flight. Recheck
		// before deleting so a transient miss or a newly published entry is not
		// treated as corrupt.
		result = ReadEntry(path, key, artifactType, schemaVersion, options);
		if (result.m_Disposition == DerivedDataReadDisposition::Miss)
		{
			m_Catalog.RemoveEntry(path);
			m_MissCount.fetch_add(1, std::memory_order_relaxed);
			return result;
		}
		if (result.m_Disposition == DerivedDataReadDisposition::Hit)
		{
			std::error_code errorCode;
			const uint64_t bytes = std::filesystem::file_size(path, errorCode);
			if (!errorCode)
				m_Catalog.RecordEntry(path, bytes);
			m_HitCount.fetch_add(1, std::memory_order_relaxed);
			m_ReadBytes.fetch_add(result.m_Payload.size(), std::memory_order_relaxed);
			return result;
		}
		m_CorruptionCount.fetch_add(1, std::memory_order_relaxed);
		std::error_code errorCode;
		if (std::filesystem::remove(path, errorCode))
		{
			m_Catalog.RemoveEntry(path);
		}
		GGLAB_LOG_GRAPHICS_WARN("Discarded corrupt local DDC entry '{}'.", path.string());
		return result;
	}

	bool LocalDerivedDataStore::Write(const DerivedDataKey& key, std::string_view artifactType,
		uint32_t schemaVersion, const ArtifactContentDigest& artifactContentDigest,
		std::span<const std::byte> payload) noexcept
	{
		if (!IsEnabled() || !key.IsValid() || artifactType.empty() ||
			!artifactContentDigest.IsValid() || payload.empty())
			return false;
		BinaryWriter header;
		header.AddBytes(ContainerMagic);
		header.AddU32(ContainerVersion);
		header.AddU32(schemaVersion);
		header.AddU32(static_cast<uint32_t>(artifactType.size()));
		header.AddU64(static_cast<uint64_t>(payload.size()));
		header.AddBytes(key.m_Value);
		header.AddBytes(artifactContentDigest.m_Value);
		header.AddBytes(ComputeSha256(payload).m_Value);
		header.AddBytes(std::as_bytes(std::span{ artifactType.data(), artifactType.size() }));

		std::scoped_lock lock(m_Mutex);
		win32::NamedMutexGuard maintenance = AcquireMaintenanceLock();
		if (!maintenance.IsAcquired())
		{
			m_WriteFailureCount.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		const std::filesystem::path path = EntryPath(key);
		if (!utils::CreateParentDirectoryIfNotExist(path))
		{
			m_WriteFailureCount.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		const std::filesystem::path temporary =
			path.string() + std::format(".tmp.{}.{}.{}", GetCurrentProcessId(),
				reinterpret_cast<uintptr_t>(this), m_TemporarySerial.fetch_add(1));
		std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
		if (stream)
		{
			stream.write(reinterpret_cast<const char*>(header.m_Bytes.data()),
				static_cast<std::streamsize>(header.m_Bytes.size()));
			stream.write(reinterpret_cast<const char*>(payload.data()),
				static_cast<std::streamsize>(payload.size()));
			stream.flush();
		}
		const bool wrote = static_cast<bool>(stream);
		stream.close();
		const uint64_t totalBytes = header.m_Bytes.size() + payload.size();
		std::error_code errorCode;
		if (!wrote)
		{
			std::filesystem::remove(temporary, errorCode);
			m_WriteFailureCount.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		constexpr uint32_t MaxPublishAttempts = 3;
		for (uint32_t attempt = 0; attempt < MaxPublishAttempts; ++attempt)
		{
			if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH))
			{
				m_Catalog.RecordEntry(path, totalBytes);
				m_WriteCount.fetch_add(1, std::memory_order_relaxed);
				m_WrittenBytes.fetch_add(payload.size(), std::memory_order_relaxed);
				return true;
			}

			DerivedDataReadResult existing = ReadEntry(path, key, artifactType, schemaVersion, {});
			if (existing.m_Disposition == DerivedDataReadDisposition::Hit)
			{
				std::filesystem::remove(temporary, errorCode);
				errorCode.clear();
				const uint64_t existingBytes = std::filesystem::file_size(path, errorCode);
				if (!errorCode)
				{
					m_Catalog.RecordEntry(path, existingBytes);
				}
				if (existing.m_ArtifactContentDigest == artifactContentDigest)
				{
					return true;
				}

				m_WriteFailureCount.fetch_add(1, std::memory_order_relaxed);
				GGLAB_LOG_GRAPHICS_ERROR(
					"Rejected non-deterministic local DDC write for key '{}' (existing artifact {}, produced artifact {}).",
					DerivedDataKeyText(key),
					ArtifactContentDigestText(existing.m_ArtifactContentDigest),
					ArtifactContentDigestText(artifactContentDigest));
				return false;
			}

			if (existing.m_Disposition == DerivedDataReadDisposition::Corrupt)
			{
				errorCode.clear();
				if (std::filesystem::remove(path, errorCode))
				{
					m_CorruptionCount.fetch_add(1, std::memory_order_relaxed);
					m_Catalog.RemoveEntry(path);
					GGLAB_LOG_GRAPHICS_WARN(
						"Discarded corrupt local DDC entry '{}' before immutable publication.",
						path.string());
					continue;
				}
			}
			break;
		}

		std::filesystem::remove(temporary, errorCode);
		m_WriteFailureCount.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	DerivedDataPresence LocalDerivedDataStore::Probe(const DerivedDataKey& key) const noexcept
	{
		if (!IsEnabled() || !key.IsValid())
			return DerivedDataPresence::Missing;
		std::error_code errorCode;
		const std::filesystem::file_status status =
			std::filesystem::status(EntryPath(key), errorCode);
		if (errorCode == std::errc::no_such_file_or_directory ||
			errorCode == std::errc::not_a_directory)
		{
			return DerivedDataPresence::Missing;
		}
		if (errorCode)
			return DerivedDataPresence::Inaccessible;
		return std::filesystem::exists(status) ? DerivedDataPresence::Present
			: DerivedDataPresence::Missing;
	}

	void LocalDerivedDataStore::DiscardObservedCorrupt(const DerivedDataKey& key,
		std::string_view artifactType, uint32_t schemaVersion,
		const ArtifactContentDigest& observedArtifactContentDigest,
		const Sha256Hash& observedPayloadDigest, LocalDerivedDataReadOptions options) noexcept
	{
		if (!IsEnabled() || !key.IsValid() || artifactType.empty() ||
			!observedArtifactContentDigest.IsValid() || !observedPayloadDigest.IsValid())
		{
			return;
		}
		std::scoped_lock lock(m_Mutex);
		win32::NamedMutexGuard maintenance = AcquireMaintenanceLock();
		if (!maintenance.IsAcquired())
			return;
		const std::filesystem::path path = EntryPath(key);
		const DerivedDataReadResult current =
			ReadEntry(path, key, artifactType, schemaVersion, options);
		if (current.m_Disposition != DerivedDataReadDisposition::Hit ||
			current.m_ArtifactContentDigest != observedArtifactContentDigest ||
			current.m_PayloadDigest.m_Value != observedPayloadDigest.m_Value)
		{
			return;
		}

		std::error_code errorCode;
		if (std::filesystem::remove(path, errorCode))
		{
			m_CorruptionCount.fetch_add(1, std::memory_order_relaxed);
			m_Catalog.RemoveEntry(path);
		}
	}

	bool LocalDerivedDataStore::Clear() noexcept
	{
		if (!IsEnabled())
			return true;
		std::vector<std::filesystem::path> trashPaths;
		bool cleared = false;
		{
			std::scoped_lock lock(m_Mutex);
			win32::NamedMutexGuard maintenance = AcquireMaintenanceLock();
			if (!maintenance.IsAcquired())
				return false;
			trashPaths = CollectTrashPathsLocked();

			std::error_code errorCode;
			const bool rootExists = std::filesystem::exists(m_RootDirectory, errorCode);
			if (errorCode)
				return false;
			if (!rootExists)
			{
				cleared = utils::CreateDirectoryIfNotExist(m_RootDirectory);
				if (cleared)
					m_Catalog.Clear();
			}
			else
			{
				const std::filesystem::path trashPath = MakeTrashPath();
				std::filesystem::rename(m_RootDirectory, trashPath, errorCode);
				if (errorCode)
				{
					GGLAB_LOG_GRAPHICS_WARN(
						"Local DDC clear could not rename '{}' to '{}': {}.",
						m_RootDirectory.string(), trashPath.string(), errorCode.message());
					return false;
				}

				if (!utils::CreateDirectoryIfNotExist(m_RootDirectory))
				{
					errorCode.clear();
					std::filesystem::rename(trashPath, m_RootDirectory, errorCode);
					GGLAB_LOG_GRAPHICS_ERROR(
						"Local DDC clear could not create replacement root '{}'; rollback {}.",
						m_RootDirectory.string(), errorCode ? "failed" : "succeeded");
					return false;
				}
				trashPaths.push_back(trashPath);
				m_Catalog.Clear();
				cleared = true;
			}
		}
		ScheduleTrashCleanup(std::move(trashPaths));
		return cleared;
	}

	bool LocalDerivedDataStore::ReconcileCatalog() noexcept
	{
		if (!IsEnabled())
			return false;
		std::scoped_lock lock(m_Mutex);
		return m_Catalog.Reconcile();
	}

	LocalDerivedDataStoreStatistics LocalDerivedDataStore::GetStatistics() const noexcept
	{
		const LocalDerivedDataCatalogSnapshot catalog = m_Catalog.GetSnapshot();
		return {
			.m_StoredBytes = catalog.m_StoredBytes,
			.m_StoredEntryCount = catalog.m_StoredEntryCount,
			.m_HitCount = m_HitCount.load(std::memory_order_relaxed),
			.m_MissCount = m_MissCount.load(std::memory_order_relaxed),
			.m_CorruptionCount = m_CorruptionCount.load(std::memory_order_relaxed),
			.m_ReadBytes = m_ReadBytes.load(std::memory_order_relaxed),
			.m_WriteCount = m_WriteCount.load(std::memory_order_relaxed),
			.m_WriteFailureCount = m_WriteFailureCount.load(std::memory_order_relaxed),
			.m_WrittenBytes = m_WrittenBytes.load(std::memory_order_relaxed),
			.m_CatalogLastReconciledAtUnixMilliseconds = catalog.m_LastReconciledAtUnixMilliseconds,
			.m_CatalogReconciliationCount = catalog.m_ReconciliationCount,
			.m_CatalogReconciliationFailureCount = catalog.m_ReconciliationFailureCount,
			.m_IsCatalogApproximate = catalog.m_IsApproximate,
		};
	}

	std::filesystem::path LocalDerivedDataStore::EntryPath(const DerivedDataKey& key) const
	{
		const std::string text = DerivedDataKeyText(key, key.m_Value.size());
		return m_RootDirectory / text.substr(0, 2) / (text + ".ddc");
	}

	win32::NamedMutexGuard LocalDerivedDataStore::AcquireMaintenanceLock() noexcept
	{
		win32::NamedMutexGuard maintenance = m_MaintenanceLock.Acquire();
		if (!maintenance.WasAbandoned())
			return maintenance;
		GGLAB_LOG_GRAPHICS_WARN("Recovered an abandoned local DDC maintenance lock for '{}'.",
			m_RootDirectory.string());
		GGLAB_UNUSED(m_Catalog.Reconcile());
		CleanupOrphanTemporaryFilesLocked();
		return maintenance;
	}

	void LocalDerivedDataStore::CleanupOrphanTemporaryFilesLocked() noexcept
	{
		std::error_code errorCode;
		for (std::filesystem::recursive_directory_iterator iterator(m_RootDirectory,
			std::filesystem::directory_options::skip_permission_denied, errorCode),
			end;
			!errorCode && iterator != end; iterator.increment(errorCode))
		{
			if (!iterator->is_regular_file(errorCode))
				continue;
			if (!iterator->path().filename().string().contains(".ddc.tmp."))
				continue;
			const std::filesystem::path temporaryPath = iterator->path();
			errorCode.clear();
			std::filesystem::remove(temporaryPath, errorCode);
			errorCode.clear();
		}
	}

	std::vector<std::filesystem::path> LocalDerivedDataStore::CollectTrashPathsLocked()
		const noexcept
	{
		std::vector<std::filesystem::path> trashPaths;
		const std::filesystem::path parent = m_RootDirectory.parent_path();
		const std::wstring prefix = m_RootDirectory.filename().wstring() + L".trash.";
		std::error_code errorCode;
		for (std::filesystem::directory_iterator iterator(
			parent, std::filesystem::directory_options::skip_permission_denied, errorCode),
			end;
			!errorCode && iterator != end; iterator.increment(errorCode))
		{
			if (iterator->path().filename().wstring().starts_with(prefix))
			{
				trashPaths.push_back(iterator->path());
			}
		}
		return trashPaths;
	}

	std::filesystem::path LocalDerivedDataStore::MakeTrashPath() noexcept
	{
		return m_RootDirectory.parent_path() /
			std::format(L"{}.trash.{}.{}.{}", m_RootDirectory.filename().wstring(),
				::GetCurrentProcessId(), ::GetTickCount64(),
				m_TrashSerial.fetch_add(1, std::memory_order_relaxed));
	}

	void LocalDerivedDataStore::ScheduleTrashCleanup(
		std::vector<std::filesystem::path> trashPaths) noexcept
	{
		if (trashPaths.empty())
			return;
		std::thread(
			[trashPaths = std::move(trashPaths)]() noexcept
			{
				for (const std::filesystem::path& trashPath : trashPaths)
				{
					std::error_code errorCode;
					std::filesystem::remove_all(trashPath, errorCode);
				}
			})
			.detach();
	}

}
