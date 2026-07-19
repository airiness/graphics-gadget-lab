#include "Core/Precompiled.h"
#include "Graphics/Asset/DerivedData/LocalDerivedDataStore.h"
#include "Core/Hash/Sha256.h"
#include "Core/Utility/PathUtils.h"

namespace gglab
{
	namespace
	{
		constexpr std::array<std::byte, 8> ContainerMagic{
			std::byte{ 'G' }, std::byte{ 'G' }, std::byte{ 'L' }, std::byte{ 'A' },
			std::byte{ 'B' }, std::byte{ 'D' }, std::byte{ 'D' }, std::byte{ 'C' },
		};
		constexpr uint32_t ContainerVersion = 1;

		class BinaryWriter
		{
		public:
			void AddBytes(std::span<const std::byte> bytes)
			{
				m_Bytes.insert(m_Bytes.end(), bytes.begin(), bytes.end());
			}
			void AddU32(uint32_t value)
			{
				for (uint32_t i = 0; i < 4; ++i) m_Bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xffu));
			}
			void AddU64(uint64_t value)
			{
				for (uint32_t i = 0; i < 8; ++i) m_Bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xffu));
			}
			std::vector<std::byte> m_Bytes;
		};

		class BinaryReader
		{
		public:
			explicit BinaryReader(std::span<const std::byte> bytes) noexcept : m_Bytes(bytes) {}
			bool ReadBytes(std::span<std::byte> output) noexcept
			{
				if (output.size() > m_Bytes.size() - m_Offset) return false;
				std::memcpy(output.data(), m_Bytes.data() + m_Offset, output.size());
				m_Offset += output.size();
				return true;
			}
			bool ReadU32(uint32_t& value) noexcept
			{
				uint64_t decoded = 0;
				if (!ReadUnsigned(decoded, 4)) return false;
				value = static_cast<uint32_t>(decoded);
				return true;
			}
			bool ReadU64(uint64_t& value) noexcept { return ReadUnsigned(value, 8); }
			[[nodiscard]] size_t Offset() const noexcept { return m_Offset; }
		private:
			bool ReadUnsigned(uint64_t& value, size_t width) noexcept
			{
				if (width > m_Bytes.size() - m_Offset) return false;
				value = 0;
				for (size_t i = 0; i < width; ++i) value |= static_cast<uint64_t>(std::to_integer<uint8_t>(m_Bytes[m_Offset + i])) << (i * 8);
				m_Offset += width;
				return true;
			}
			std::span<const std::byte> m_Bytes;
			size_t m_Offset = 0;
		};
	}

	LocalDerivedDataStore::LocalDerivedDataStore(
		std::filesystem::path rootDirectory) noexcept :
		m_RootDirectory(std::move(rootDirectory))
	{
		if (IsEnabled())
		{
			GGLAB_UNUSED(utils::CreateDirectoryIfNotExist(m_RootDirectory));
			RefreshStoredStatistics();
		}
	}

	DerivedDataReadResult LocalDerivedDataStore::Read(
		const DerivedDataKey& key,
		std::string_view artifactType,
		uint32_t schemaVersion) noexcept
	{
		DerivedDataReadResult result{};
		if (!IsEnabled() || !key.IsValid())
		{
			m_MissCount.fetch_add(1, std::memory_order_relaxed);
			return result;
		}
		std::scoped_lock lock(m_Mutex);
		const std::filesystem::path path = EntryPath(key);
		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		if (!stream)
		{
			m_EntryPaths.erase(path);
			m_MissCount.fetch_add(1, std::memory_order_relaxed);
			return result;
		}
		const std::streamoff end = stream.tellg();
		if (end <= 0 || static_cast<uint64_t>(end) > std::numeric_limits<size_t>::max())
		{
			result.m_Disposition = DerivedDataReadDisposition::Corrupt;
		}
		else
		{
			std::vector<std::byte> fileBytes(static_cast<size_t>(end));
			stream.seekg(0, std::ios::beg);
			stream.read(reinterpret_cast<char*>(fileBytes.data()), static_cast<std::streamsize>(fileBytes.size()));
			BinaryReader reader(fileBytes);
			std::array<std::byte, 8> magic{};
			uint32_t containerVersion = 0;
			uint32_t storedSchema = 0;
			uint32_t typeBytes = 0;
			uint64_t payloadBytes = 0;
			DerivedDataKey storedKey{};
			ArtifactContentDigest storedArtifactDigest{};
			Sha256Hash storedPayloadDigest{};
			const bool headerValid = stream &&
				reader.ReadBytes(magic) && reader.ReadU32(containerVersion) &&
				reader.ReadU32(storedSchema) && reader.ReadU32(typeBytes) &&
				reader.ReadU64(payloadBytes) && reader.ReadBytes(storedKey.m_Value) &&
				reader.ReadBytes(storedArtifactDigest.m_Value) &&
				reader.ReadBytes(storedPayloadDigest.m_Value);
			const uint64_t headerBytes = reader.Offset();
			const bool sizesValid = headerBytes <= fileBytes.size() &&
				typeBytes <= fileBytes.size() - headerBytes &&
				payloadBytes <= fileBytes.size() - headerBytes - typeBytes;
			if (headerValid && magic == ContainerMagic && containerVersion == ContainerVersion &&
				storedSchema == schemaVersion && storedKey == key &&
				typeBytes == artifactType.size() && sizesValid &&
				headerBytes + typeBytes + payloadBytes == fileBytes.size())
			{
				const auto storedType = std::span(fileBytes).subspan(reader.Offset(), typeBytes);
				const auto expectedType = std::as_bytes(std::span{ artifactType.data(), artifactType.size() });
				const auto payload = std::span(fileBytes).subspan(reader.Offset() + typeBytes, static_cast<size_t>(payloadBytes));
				if (std::ranges::equal(storedType, expectedType) &&
					ComputeSha256(payload).m_Value == storedPayloadDigest.m_Value &&
					storedArtifactDigest.IsValid())
				{
					result.m_Disposition = DerivedDataReadDisposition::Hit;
					result.m_ArtifactContentDigest = storedArtifactDigest;
					result.m_Payload.assign(payload.begin(), payload.end());
				}
			}
			if (result.m_Disposition != DerivedDataReadDisposition::Hit)
			{
				result.m_Disposition = DerivedDataReadDisposition::Corrupt;
			}
		}
		stream.close();

		if (result.m_Disposition == DerivedDataReadDisposition::Hit)
		{
			m_HitCount.fetch_add(1, std::memory_order_relaxed);
			m_ReadBytes.fetch_add(result.m_Payload.size(), std::memory_order_relaxed);
			return result;
		}
		m_CorruptionCount.fetch_add(1, std::memory_order_relaxed);
		std::error_code errorCode;
		if (std::filesystem::remove(path, errorCode))
		{
			m_EntryPaths.erase(path);
			RefreshStoredStatistics();
		}
		GGLAB_LOG_GRAPHICS_WARN("Discarded corrupt local DDC entry '{}'.", path.string());
		return result;
	}

	bool LocalDerivedDataStore::Write(
		const DerivedDataKey& key,
		std::string_view artifactType,
		uint32_t schemaVersion,
		const ArtifactContentDigest& artifactContentDigest,
		std::span<const std::byte> payload) noexcept
	{
		if (!IsEnabled() || !key.IsValid() || artifactType.empty() ||
			!artifactContentDigest.IsValid() || payload.empty()) return false;
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
		const std::filesystem::path path = EntryPath(key);
		if (!utils::CreateParentDirectoryIfNotExist(path))
		{
			m_WriteFailureCount.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		const std::filesystem::path temporary = path.string() + std::format(
			".tmp.{}.{}", GetCurrentProcessId(), m_TemporarySerial.fetch_add(1));
		std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
		if (stream)
		{
			stream.write(reinterpret_cast<const char*>(header.m_Bytes.data()), static_cast<std::streamsize>(header.m_Bytes.size()));
			stream.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
			stream.flush();
		}
		const bool wrote = static_cast<bool>(stream);
		stream.close();
		const uint64_t totalBytes = header.m_Bytes.size() + payload.size();
		std::error_code errorCode;
		const bool existed = std::filesystem::exists(path, errorCode);
		const uint64_t previousBytes = existed ? std::filesystem::file_size(path, errorCode) : 0;
		const bool published = wrote && MoveFileExW(
			temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
		if (!published)
		{
			std::filesystem::remove(temporary, errorCode);
			m_WriteFailureCount.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		if (!existed) m_StoredEntryCount.fetch_add(1, std::memory_order_relaxed);
		m_EntryPaths.insert(path);
		if (previousBytes <= m_StoredBytes.load(std::memory_order_relaxed)) m_StoredBytes.fetch_sub(previousBytes, std::memory_order_relaxed);
		m_StoredBytes.fetch_add(totalBytes, std::memory_order_relaxed);
		m_WriteCount.fetch_add(1, std::memory_order_relaxed);
		m_WrittenBytes.fetch_add(payload.size(), std::memory_order_relaxed);
		return true;
	}

	bool LocalDerivedDataStore::Contains(const DerivedDataKey& key) const noexcept
	{
		if (!IsEnabled() || !key.IsValid()) return false;
		std::scoped_lock lock(m_Mutex);
		return m_EntryPaths.contains(EntryPath(key));
	}

	void LocalDerivedDataStore::DiscardCorrupt(const DerivedDataKey& key) noexcept
	{
		if (!IsEnabled() || !key.IsValid()) return;
		std::scoped_lock lock(m_Mutex);
		std::error_code errorCode;
		if (std::filesystem::remove(EntryPath(key), errorCode))
		{
			m_CorruptionCount.fetch_add(1, std::memory_order_relaxed);
			m_EntryPaths.erase(EntryPath(key));
			RefreshStoredStatistics();
		}
	}

	void LocalDerivedDataStore::Clear() noexcept
	{
		if (!IsEnabled()) return;
		std::scoped_lock lock(m_Mutex);
		std::error_code errorCode;
		std::filesystem::remove_all(m_RootDirectory, errorCode);
		GGLAB_UNUSED(utils::CreateDirectoryIfNotExist(m_RootDirectory));
		m_EntryPaths.clear();
		m_StoredBytes.store(0, std::memory_order_relaxed);
		m_StoredEntryCount.store(0, std::memory_order_relaxed);
	}

	LocalDerivedDataStoreStatistics LocalDerivedDataStore::GetStatistics() const noexcept
	{
		return {
			.m_StoredBytes = m_StoredBytes.load(std::memory_order_relaxed),
			.m_StoredEntryCount = m_StoredEntryCount.load(std::memory_order_relaxed),
			.m_HitCount = m_HitCount.load(std::memory_order_relaxed),
			.m_MissCount = m_MissCount.load(std::memory_order_relaxed),
			.m_CorruptionCount = m_CorruptionCount.load(std::memory_order_relaxed),
			.m_ReadBytes = m_ReadBytes.load(std::memory_order_relaxed),
			.m_WriteCount = m_WriteCount.load(std::memory_order_relaxed),
			.m_WriteFailureCount = m_WriteFailureCount.load(std::memory_order_relaxed),
			.m_WrittenBytes = m_WrittenBytes.load(std::memory_order_relaxed),
		};
	}

	std::filesystem::path LocalDerivedDataStore::EntryPath(const DerivedDataKey& key) const
	{
		const std::string text = DerivedDataKeyText(key, key.m_Value.size());
		return m_RootDirectory / text.substr(0, 2) / (text + ".ddc");
	}

	void LocalDerivedDataStore::RefreshStoredStatistics() noexcept
	{
		uint64_t entries = 0;
		uint64_t bytes = 0;
		m_EntryPaths.clear();
		std::vector<std::filesystem::path> temporaryFiles;
		std::error_code errorCode;
		for (std::filesystem::recursive_directory_iterator iterator(m_RootDirectory, errorCode), end;
			!errorCode && iterator != end; iterator.increment(errorCode))
		{
			if (!iterator->is_regular_file(errorCode)) continue;
			if (iterator->path().extension() == ".ddc")
			{
				++entries;
				bytes += iterator->file_size(errorCode);
				m_EntryPaths.insert(iterator->path());
			}
			else if (iterator->path().filename().string().contains(".ddc.tmp."))
			{
				temporaryFiles.push_back(iterator->path());
			}
		}
		for (const std::filesystem::path& temporary : temporaryFiles)
		{
			errorCode.clear();
			std::filesystem::remove(temporary, errorCode);
		}
		m_StoredEntryCount.store(entries, std::memory_order_relaxed);
		m_StoredBytes.store(bytes, std::memory_order_relaxed);
	}
}
