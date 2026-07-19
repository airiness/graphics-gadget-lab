#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/Asset/IBLBundleArtifact.h"

#include <atomic>
#include <memory>
#include <unordered_map>

namespace gglab
{
	struct IBLBundleArtifactCacheConfig
	{
		uint64_t m_BudgetBytes = 1024ull * 1024ull * 1024ull;
	};

	struct IBLBundleArtifactCacheStatistics
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

	class IBLBundleArtifactCache final
	{
	public:
		explicit IBLBundleArtifactCache(
			const IBLBundleArtifactCacheConfig& config = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(IBLBundleArtifactCache);
		~IBLBundleArtifactCache() = default;

		[[nodiscard]] IBLBundleArtifactHandle Admit(
			IBLBundleArtifactHandle artifact) noexcept;
		[[nodiscard]] IBLBundleArtifactHandle Find(
			const ArtifactContentDigest& contentDigest) noexcept;
		[[nodiscard]] bool Contains(
			const ArtifactContentDigest& contentDigest) const noexcept;
		void Clear() noexcept;
		[[nodiscard]] IBLBundleArtifactCacheStatistics GetStatistics() const noexcept;

	private:
		struct Entry
		{
			IBLBundleArtifactHandle m_Artifact;
			uint64_t m_Bytes = 0;
			uint64_t m_LastAccessSerial = 0;
		};

		struct LiveState
		{
			std::atomic_uint64_t m_Bytes = 0;
		};

		void EvictToFit(uint64_t incomingBytes) noexcept;
		void EvictOne() noexcept;

		IBLBundleArtifactCacheConfig m_Config{};
		std::shared_ptr<LiveState> m_LiveState;
		std::unordered_map<ArtifactContentDigest, Entry, ArtifactContentDigestHash> m_Entries;
		uint64_t m_AccessSerial = 0;
		uint64_t m_CachedBytes = 0;
		uint64_t m_HitCount = 0;
		uint64_t m_MissCount = 0;
		uint64_t m_AdmissionCount = 0;
		uint64_t m_AdmissionRejectedCount = 0;
		uint64_t m_EvictionCount = 0;
		uint64_t m_EvictedBytes = 0;
	};
}
