#pragma once
#include <cstdint>

namespace gglab
{
	// Cache accounting values shared by artifact caches and the derived-data
	// store. The counters are copied snapshots; no cache state is borrowed.
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
}
