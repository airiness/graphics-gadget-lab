#pragma once
#include "Core/CoreMacros.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <unordered_map>
#include <utility>

namespace gglab
{
	struct ArtifactCacheCoreStatistics
	{
		uint64_t m_BudgetBytes = 0;
		uint64_t m_CachedBytes = 0;
		uint64_t m_ExternallyRetainedBytes = 0;
		uint64_t m_TotalLiveBytes = 0;
		uint32_t m_CachedEntryCount = 0;
		uint64_t m_HitCount = 0;
		uint64_t m_MissCount = 0;
		uint64_t m_AdmissionCount = 0;
		uint64_t m_AdmissionRejectedCount = 0;
		uint64_t m_EvictionCount = 0;
		uint64_t m_EvictedBytes = 0;
	};

	// Wrappers own artifact validation and key selection. The core derives the
	// physical allocation size from the immutable artifact, then keeps cache
	// entries and allocation records on the same lifetime model. Records remain
	// discoverable through external handles so re-admission cannot count the same
	// physical allocation twice after eviction.
	template<typename Key, typename Artifact, typename KeyHash = std::hash<Key>>
	class ArtifactCacheCore final
	{
	public:
		using Handle = std::shared_ptr<const Artifact>;

		explicit ArtifactCacheCore(uint64_t budgetBytes) noexcept :
			m_BudgetBytes(budgetBytes),
			m_LiveState(std::make_shared<LiveState>())
		{}
		GGLAB_DELETE_COPYABLE_MOVABLE(ArtifactCacheCore);
		~ArtifactCacheCore() = default;

		[[nodiscard]] Handle Admit(
			const Key& key,
			Handle artifact) noexcept
		{
			if (!artifact)
			{
				return {};
			}

			if (auto existing = m_Entries.find(key); existing != m_Entries.end())
			{
				if (existing->second.m_Artifact.get() == artifact.get())
				{
					GGLAB_ASSERT_MSG(
						existing->second.m_PhysicalBytes == artifact->GetAllocatedBytes(),
						"Repeated artifact handle admission changed the physical byte estimate.");
				}
				Touch(existing->second);
				return existing->second.m_Artifact;
			}

			AssertNoPointerAlias(artifact.get());
			const uint64_t physicalBytes = artifact->GetAllocatedBytes();
			if (physicalBytes == 0)
			{
				return {};
			}
			Handle trackedArtifact = TrackAllocation(
				std::move(artifact),
				physicalBytes);
			if (physicalBytes > m_BudgetBytes)
			{
				++m_AdmissionRejectedCount;
				return trackedArtifact;
			}

			EvictToFit(physicalBytes);
			if (physicalBytes > m_BudgetBytes - m_CachedBytes)
			{
				++m_AdmissionRejectedCount;
				return trackedArtifact;
			}

			m_Lru.push_front(key);
			const auto [entry, inserted] = m_Entries.emplace(
				key,
				Entry{
					.m_Artifact = trackedArtifact,
					.m_PhysicalBytes = physicalBytes,
					.m_Lru = m_Lru.begin(),
				});
			GGLAB_ASSERT_MSG(inserted, "Artifact cache key admission must be unique.");
			if (!inserted)
			{
				m_Lru.pop_front();
				return entry->second.m_Artifact;
			}

			m_CachedBytes += physicalBytes;
			++m_AdmissionCount;
			return trackedArtifact;
		}

		[[nodiscard]] Handle Find(const Key& key) noexcept
		{
			const auto entry = m_Entries.find(key);
			if (entry == m_Entries.end())
			{
				++m_MissCount;
				return {};
			}

			Touch(entry->second);
			++m_HitCount;
			return entry->second.m_Artifact;
		}

		[[nodiscard]] bool Contains(const Key& key) const noexcept
		{
			return m_Entries.contains(key);
		}

		void Clear() noexcept
		{
			while (!m_Entries.empty())
			{
				EvictOne();
			}
		}

		[[nodiscard]] ArtifactCacheCoreStatistics GetStatistics() const noexcept
		{
			const uint64_t liveBytes =
				m_LiveState->m_Bytes.load(std::memory_order_relaxed);
			return {
				.m_BudgetBytes = m_BudgetBytes,
				.m_CachedBytes = m_CachedBytes,
				.m_ExternallyRetainedBytes = liveBytes > m_CachedBytes ?
					liveBytes - m_CachedBytes : 0,
				.m_TotalLiveBytes = liveBytes,
				.m_CachedEntryCount = static_cast<uint32_t>(m_Entries.size()),
				.m_HitCount = m_HitCount,
				.m_MissCount = m_MissCount,
				.m_AdmissionCount = m_AdmissionCount,
				.m_AdmissionRejectedCount = m_AdmissionRejectedCount,
				.m_EvictionCount = m_EvictionCount,
				.m_EvictedBytes = m_EvictedBytes,
			};
		}

	private:
		using LruList = std::list<Key>;

		struct Entry
		{
			Handle m_Artifact;
			uint64_t m_PhysicalBytes = 0;
			typename LruList::iterator m_Lru;
		};

		struct LiveState
		{
			std::atomic_uint64_t m_Bytes = 0;
		};

		struct AllocationRecord
		{
			AllocationRecord(
				std::shared_ptr<LiveState> liveState,
				Handle artifact,
				uint64_t physicalBytes) noexcept :
				m_LiveState(std::move(liveState)),
				m_Artifact(std::move(artifact)),
				m_PhysicalBytes(physicalBytes)
			{
				m_LiveState->m_Bytes.fetch_add(
					m_PhysicalBytes,
					std::memory_order_relaxed);
			}

			~AllocationRecord()
			{
				m_LiveState->m_Bytes.fetch_sub(
					m_PhysicalBytes,
					std::memory_order_relaxed);
			}

			std::shared_ptr<LiveState> m_LiveState;
			Handle m_Artifact;
			uint64_t m_PhysicalBytes = 0;
		};

		struct TrackedArtifactDeleter
		{
			void operator()(const Artifact*) const noexcept {}

			std::shared_ptr<AllocationRecord> m_Allocation;
		};

		void AssertNoPointerAlias(const Artifact* artifact) const noexcept
		{
#ifndef NDEBUG
			for (const auto& [existingKey, entry] : m_Entries)
			{
				GGLAB_UNUSED(existingKey);
				GGLAB_ASSERT_MSG(
					entry.m_Artifact.get() != artifact,
					"One artifact allocation cannot be admitted under multiple cache keys.");
			}
#else
			GGLAB_UNUSED(artifact);
#endif
		}

		[[nodiscard]] Handle TrackAllocation(
			Handle artifact,
			uint64_t physicalBytes) noexcept
		{
			if (const TrackedArtifactDeleter* tracked =
				std::get_deleter<TrackedArtifactDeleter>(artifact);
				tracked && tracked->m_Allocation &&
				tracked->m_Allocation->m_LiveState == m_LiveState)
			{
				GGLAB_ASSERT_MSG(
					tracked->m_Allocation->m_Artifact.get() == artifact.get(),
					"Tracked artifact allocation changed its physical address.");
				GGLAB_ASSERT_MSG(
					tracked->m_Allocation->m_PhysicalBytes == physicalBytes,
					"Tracked artifact allocation changed its physical byte estimate.");
				return artifact;
			}

			const Artifact* value = artifact.get();
			std::shared_ptr<AllocationRecord> allocation =
				std::make_shared<AllocationRecord>(
					m_LiveState,
					std::move(artifact),
					physicalBytes);
			return Handle(
				value,
				TrackedArtifactDeleter{
					.m_Allocation = std::move(allocation),
				});
		}

		void Touch(Entry& entry) noexcept
		{
			m_Lru.splice(m_Lru.begin(), m_Lru, entry.m_Lru);
			entry.m_Lru = m_Lru.begin();
		}

		void EvictToFit(uint64_t incomingBytes) noexcept
		{
			while (!m_Entries.empty() && incomingBytes > m_BudgetBytes - m_CachedBytes)
			{
				EvictOne();
			}
		}

		void EvictOne() noexcept
		{
			if (m_Lru.empty())
			{
				GGLAB_ASSERT(m_Entries.empty());
				return;
			}

			const auto entry = m_Entries.find(m_Lru.back());
			GGLAB_ASSERT_MSG(entry != m_Entries.end(), "Artifact cache LRU entry is stale.");
			if (entry == m_Entries.end())
			{
				m_Lru.pop_back();
				return;
			}

			const uint64_t physicalBytes = entry->second.m_PhysicalBytes;
			GGLAB_ASSERT(physicalBytes <= m_CachedBytes);
			m_CachedBytes = physicalBytes <= m_CachedBytes ?
				m_CachedBytes - physicalBytes : 0;
			++m_EvictionCount;
			m_EvictedBytes += physicalBytes;
			m_Entries.erase(entry);
			m_Lru.pop_back();
		}

		uint64_t m_BudgetBytes = 0;
		std::shared_ptr<LiveState> m_LiveState;
		std::unordered_map<Key, Entry, KeyHash> m_Entries;
		LruList m_Lru;
		uint64_t m_CachedBytes = 0;
		uint64_t m_HitCount = 0;
		uint64_t m_MissCount = 0;
		uint64_t m_AdmissionCount = 0;
		uint64_t m_AdmissionRejectedCount = 0;
		uint64_t m_EvictionCount = 0;
		uint64_t m_EvictedBytes = 0;
	};
}
