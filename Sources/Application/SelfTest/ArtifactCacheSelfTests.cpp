#include "Core/Precompiled.h"
#include "Application/SelfTest/ArtifactCacheSelfTests.h"
#include "Graphics/Asset/ArtifactCacheCore.h"

namespace gglab
{
	namespace
	{
		struct CacheTestArtifact
		{
			uint32_t m_Id = 0;
		};

		using CacheTestCore = ArtifactCacheCore<uint32_t, CacheTestArtifact>;
		using CacheTestHandle = std::shared_ptr<const CacheTestArtifact>;

		[[nodiscard]] CacheTestHandle Admit(
			CacheTestCore& cache,
			uint32_t key,
			uint32_t artifactId,
			uint64_t physicalBytes) noexcept
		{
			return cache.Admit(
				key,
				{
					.m_Artifact = std::make_shared<const CacheTestArtifact>(
						CacheTestArtifact{ .m_Id = artifactId }),
					.m_PhysicalBytes = physicalBytes,
				});
		}

		void RunDuplicateAdmissionTests(SelfTestContext& context) noexcept
		{
			CacheTestCore cache(8);
			CacheTestHandle first = Admit(cache, 1, 10, 4);
			CacheTestHandle duplicate = Admit(cache, 1, 20, 4);
			const ArtifactCacheCoreStatistics statistics = cache.GetStatistics();
			context.Check(
				first && duplicate && first == duplicate && duplicate->m_Id == 10,
				"Artifact cache keeps one canonical artifact for a repeated key");
			context.Check(
				statistics.m_AdmissionCount == 1 &&
					statistics.m_CachedEntryCount == 1 &&
					statistics.m_CachedBytes == 4 && statistics.m_TotalLiveBytes == 4,
				"Artifact cache repeated admission preserves physical byte accounting");

			first.reset();
			duplicate.reset();
			cache.Clear();
			context.Check(
				cache.GetStatistics().m_TotalLiveBytes == 0,
				"Artifact cache releases its allocation ticket after the last handle");
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
				cache.Contains(1) && !cache.Contains(2) && cache.Contains(3) &&
					!evictedLookup,
				"Artifact cache evicts the least-recently-used key in O(1) LRU order");
			context.Check(
				statistics.m_CachedBytes == 8 && statistics.m_TotalLiveBytes == 12 &&
					statistics.m_ExternallyRetainedBytes == 4,
				"Artifact cache reports an evicted artifact retained by an external handle");
			context.Check(
				statistics.m_HitCount == 1 && statistics.m_MissCount == 1 &&
					statistics.m_EvictionCount == 1 && statistics.m_EvictedBytes == 4,
				"Artifact cache records lookup and eviction counters");

			second.reset();
			statistics = cache.GetStatistics();
			context.Check(
				statistics.m_TotalLiveBytes == 8 &&
					statistics.m_ExternallyRetainedBytes == 0,
				"Artifact cache drops live bytes when an evicted external pin is released");

			cache.Clear();
			statistics = cache.GetStatistics();
			context.Check(
				statistics.m_CachedEntryCount == 0 && statistics.m_CachedBytes == 0 &&
					statistics.m_TotalLiveBytes == 8 &&
					statistics.m_ExternallyRetainedBytes == 8,
				"Artifact cache clear preserves externally pinned allocation tickets");

			first.reset();
			touched.reset();
			third.reset();
			context.Check(
				cache.GetStatistics().m_TotalLiveBytes == 0,
				"Artifact cache live bytes reach zero after all external pins are released");
		}

		void RunRejectedAdmissionTests(SelfTestContext& context) noexcept
		{
			CacheTestCore cache(8);
			CacheTestHandle oversized = Admit(cache, 1, 10, 9);
			ArtifactCacheCoreStatistics statistics = cache.GetStatistics();
			context.Check(
				oversized && !cache.Contains(1) &&
					statistics.m_AdmissionRejectedCount == 1 &&
					statistics.m_CachedEntryCount == 0,
				"Artifact cache returns an oversized artifact without caching it");
			context.Check(
				statistics.m_TotalLiveBytes == 9 &&
					statistics.m_ExternallyRetainedBytes == 9,
				"Artifact cache tracks an uncached oversized artifact as externally retained");

			oversized.reset();
			context.Check(
				cache.GetStatistics().m_TotalLiveBytes == 0,
				"Artifact cache releases a rejected admission ticket with its handle");
		}

		void RunCoreLifetimeTests(SelfTestContext& context) noexcept
		{
			CacheTestHandle retained;
			{
				CacheTestCore cache(8);
				retained = Admit(cache, 1, 10, 4);
			}
			context.Check(
				retained && retained->m_Id == 10,
				"Artifact cache external handles safely outlive the cache core");
			retained.reset();
		}
	}

	void RunArtifactCacheSelfTests(SelfTestContext& context) noexcept
	{
		RunDuplicateAdmissionTests(context);
		RunLruAndLifetimeTests(context);
		RunRejectedAdmissionTests(context);
		RunCoreLifetimeTests(context);
	}
}
