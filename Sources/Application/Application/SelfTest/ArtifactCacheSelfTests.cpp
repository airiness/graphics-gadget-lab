#include "Application/SelfTest/ArtifactCacheSelfTests.h"
#include "Graphics/Asset/ArtifactCacheCore.h"

namespace gglab
{
	namespace
	{
		struct CacheTestArtifact
		{
			uint32_t m_Id = 0;
			uint64_t m_AllocatedBytes = 0;

			[[nodiscard]] uint64_t GetAllocatedBytes() const noexcept { return m_AllocatedBytes; }
		};

		using CacheTestCore = ArtifactCacheCore<uint32_t, CacheTestArtifact>;
		using CacheTestHandle = std::shared_ptr<const CacheTestArtifact>;

		[[nodiscard]] CacheTestHandle Admit(CacheTestCore& cache, uint32_t key, uint32_t artifactId,
			uint64_t physicalBytes) noexcept
		{
			return cache.Admit(key, std::make_shared<const CacheTestArtifact>(CacheTestArtifact{
										.m_Id = artifactId,
										.m_AllocatedBytes = physicalBytes,
				}));
		}

		void RunDuplicateAdmissionTests(SelfTestContext& context) noexcept
		{
			CacheTestCore cache(8);
			CacheTestHandle first = Admit(cache, 1, 10, 4);
			CacheTestHandle duplicate = Admit(cache, 1, 20, 6);
			const ArtifactCacheCoreStatistics statistics = cache.GetStatistics();
			context.Check(first && duplicate && first == duplicate && duplicate->m_Id == 10,
				"Artifact cache keeps one canonical artifact for a repeated key");
			context.Check(statistics.m_AdmissionCount == 1 && statistics.m_CachedEntryCount == 1 &&
				statistics.m_CachedBytes == 4 && statistics.m_TotalLiveBytes == 4,
				"Artifact cache repeated keys preserve canonical physical byte accounting");

			first.reset();
			duplicate.reset();
			cache.Clear();
			context.Check(cache.GetStatistics().m_TotalLiveBytes == 0,
				"Artifact cache releases its allocation ticket after the last handle");
		}

		void RunCrossKeyAliasTests(SelfTestContext& context) noexcept
		{
			CacheTestCore cache(8);
			CacheTestHandle artifact = std::make_shared<const CacheTestArtifact>(CacheTestArtifact{
				.m_Id = 10,
				.m_AllocatedBytes = 4,
				});
			CacheTestHandle first = cache.Admit(1, artifact);
			CacheTestHandle alias = cache.Admit(2, artifact);
			const ArtifactCacheCoreStatistics statistics = cache.GetStatistics();
			context.Check(first && alias == first && cache.Contains(1) && !cache.Contains(2),
				"Artifact cache rejects one allocation admitted under different keys");
			context.Check(statistics.m_AdmissionCount == 1 &&
				statistics.m_AdmissionRejectedCount == 1 &&
				statistics.m_CachedEntryCount == 1 && statistics.m_CachedBytes == 4 &&
				statistics.m_TotalLiveBytes == 4,
				"Artifact cache cross-key alias rejection preserves physical byte accounting");

			artifact.reset();
			first.reset();
			alias.reset();
			cache.Clear();
			context.Check(cache.GetStatistics().m_TotalLiveBytes == 0,
				"Artifact cache releases a rejected cross-key alias exactly once");
		}

		void RunLruAndLifetimeTests(SelfTestContext& context) noexcept
		{
			CacheTestCore cache(8);
			CacheTestHandle first = Admit(cache, 1, 10, 4);
			CacheTestHandle second = Admit(cache, 2, 20, 4);
			CacheTestHandle touched = cache.Find(1);
			CacheTestHandle third = Admit(cache, 3, 30, 4);
			CacheTestHandle evictedLookup = cache.Find(2);
			ArtifactCacheCoreStatistics statistics = cache.GetStatistics();
			context.Check(
				cache.Contains(1) && !cache.Contains(2) && cache.Contains(3) && !evictedLookup,
				"Artifact cache evicts the least-recently-used key in O(1) LRU order");
			context.Check(statistics.m_CachedBytes == 8 && statistics.m_TotalLiveBytes == 12 &&
				statistics.m_ExternallyRetainedBytes == 4,
				"Artifact cache reports an evicted artifact retained by an external handle");
			context.Check(statistics.m_HitCount == 1 && statistics.m_MissCount == 1 &&
				statistics.m_EvictionCount == 1 && statistics.m_EvictedBytes == 4,
				"Artifact cache records lookup and eviction counters");

			second.reset();
			statistics = cache.GetStatistics();
			context.Check(
				statistics.m_TotalLiveBytes == 8 && statistics.m_ExternallyRetainedBytes == 0,
				"Artifact cache drops live bytes when an evicted external pin is released");

			cache.Clear();
			statistics = cache.GetStatistics();
			context.Check(statistics.m_CachedEntryCount == 0 && statistics.m_CachedBytes == 0 &&
				statistics.m_TotalLiveBytes == 8 &&
				statistics.m_ExternallyRetainedBytes == 8,
				"Artifact cache clear preserves externally pinned allocation tickets");

			first.reset();
			touched.reset();
			third.reset();
			context.Check(cache.GetStatistics().m_TotalLiveBytes == 0,
				"Artifact cache live bytes reach zero after all external pins are released");
		}

		void RunReadmissionTests(SelfTestContext& context) noexcept
		{
			CacheTestCore cache(8);
			CacheTestHandle retained = Admit(cache, 1, 10, 4);
			cache.Clear();
			ArtifactCacheCoreStatistics statistics = cache.GetStatistics();
			context.Check(retained && !cache.Contains(1) && statistics.m_CachedBytes == 0 &&
				statistics.m_TotalLiveBytes == 4 &&
				statistics.m_ExternallyRetainedBytes == 4,
				"Artifact cache retains one allocation record after eviction");

			CacheTestHandle readmitted = cache.Admit(1, retained);
			statistics = cache.GetStatistics();
			context.Check(readmitted == retained && cache.Contains(1) &&
				statistics.m_AdmissionCount == 2 && statistics.m_CachedBytes == 4 &&
				statistics.m_TotalLiveBytes == 4 &&
				statistics.m_ExternallyRetainedBytes == 0,
				"Artifact cache re-admission reuses the retained allocation record");

			retained.reset();
			readmitted.reset();
			cache.Clear();
			context.Check(cache.GetStatistics().m_TotalLiveBytes == 0,
				"Artifact cache releases a re-admitted allocation record exactly once");
		}

		void RunRejectedAdmissionTests(SelfTestContext& context) noexcept
		{
			CacheTestCore cache(8);
			CacheTestHandle oversized = Admit(cache, 1, 10, 9);
			ArtifactCacheCoreStatistics statistics = cache.GetStatistics();
			context.Check(oversized && !cache.Contains(1) &&
				statistics.m_AdmissionRejectedCount == 1 &&
				statistics.m_CachedEntryCount == 0,
				"Artifact cache returns an oversized artifact without caching it");
			context.Check(
				statistics.m_TotalLiveBytes == 9 && statistics.m_ExternallyRetainedBytes == 9,
				"Artifact cache tracks an uncached oversized artifact as externally retained");

			oversized.reset();
			context.Check(cache.GetStatistics().m_TotalLiveBytes == 0,
				"Artifact cache releases a rejected admission ticket with its handle");
		}

		void RunCoreLifetimeTests(SelfTestContext& context) noexcept
		{
			CacheTestHandle retained;
			{
				CacheTestCore cache(8);
				retained = Admit(cache, 1, 10, 4);
			}
			context.Check(retained && retained->m_Id == 10,
				"Artifact cache external handles safely outlive the cache core");
			retained.reset();
		}
	}

	void RunArtifactCacheSelfTests(SelfTestContext& context) noexcept
	{
		RunDuplicateAdmissionTests(context);
		RunCrossKeyAliasTests(context);
		RunLruAndLifetimeTests(context);
		RunReadmissionTests(context);
		RunRejectedAdmissionTests(context);
		RunCoreLifetimeTests(context);
	}
}
