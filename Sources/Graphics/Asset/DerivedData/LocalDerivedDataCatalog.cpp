#include "Core/Precompiled.h"
#include "Graphics/Asset/DerivedData/LocalDerivedDataCatalog.h"

#include <chrono>

namespace gglab
{
	namespace
	{
		[[nodiscard]] uint64_t CurrentUnixMilliseconds() noexcept
		{
			return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		}
	}

	LocalDerivedDataCatalog::LocalDerivedDataCatalog(
		std::filesystem::path rootDirectory) noexcept :
		m_RootDirectory(std::move(rootDirectory))
	{
	}

	bool LocalDerivedDataCatalog::Reconcile() noexcept
	{
		std::unordered_map<std::filesystem::path, uint64_t> entries;
		uint64_t storedBytes = 0;
		std::error_code errorCode;
		std::filesystem::recursive_directory_iterator iterator(
			m_RootDirectory,
			std::filesystem::directory_options::skip_permission_denied,
			errorCode);
		const std::filesystem::recursive_directory_iterator end;
		while (!errorCode && iterator != end)
		{
			const std::filesystem::directory_entry& entry = *iterator;
			if (entry.path().extension() == ".ddc" && entry.is_regular_file(errorCode))
			{
				const uint64_t bytes = entry.file_size(errorCode);
				if (!errorCode)
				{
					entries.emplace(entry.path(), bytes);
					storedBytes += bytes;
				}
			}
			if (!errorCode)
			{
				iterator.increment(errorCode);
			}
		}

		std::scoped_lock lock(m_Mutex);
		if (errorCode)
		{
			++m_ReconciliationFailureCount;
			return false;
		}
		m_Entries = std::move(entries);
		m_StoredBytes = storedBytes;
		m_LastReconciledAtUnixMilliseconds = CurrentUnixMilliseconds();
		++m_ReconciliationCount;
		return true;
	}

	void LocalDerivedDataCatalog::RecordEntry(
		const std::filesystem::path& path,
		uint64_t bytes) noexcept
	{
		std::scoped_lock lock(m_Mutex);
		auto [iterator, inserted] = m_Entries.try_emplace(path, bytes);
		if (inserted)
		{
			m_StoredBytes += bytes;
			return;
		}
		m_StoredBytes -= iterator->second;
		iterator->second = bytes;
		m_StoredBytes += bytes;
	}

	void LocalDerivedDataCatalog::RemoveEntry(const std::filesystem::path& path) noexcept
	{
		std::scoped_lock lock(m_Mutex);
		const auto iterator = m_Entries.find(path);
		if (iterator == m_Entries.end()) return;
		m_StoredBytes -= iterator->second;
		m_Entries.erase(iterator);
	}

	void LocalDerivedDataCatalog::Clear() noexcept
	{
		std::scoped_lock lock(m_Mutex);
		m_Entries.clear();
		m_StoredBytes = 0;
	}

	LocalDerivedDataCatalogSnapshot LocalDerivedDataCatalog::GetSnapshot() const noexcept
	{
		std::scoped_lock lock(m_Mutex);
		return {
			.m_StoredBytes = m_StoredBytes,
			.m_StoredEntryCount = m_Entries.size(),
			.m_LastReconciledAtUnixMilliseconds = m_LastReconciledAtUnixMilliseconds,
			.m_ReconciliationCount = m_ReconciliationCount,
			.m_ReconciliationFailureCount = m_ReconciliationFailureCount,
			.m_IsApproximate = true,
		};
	}
}
