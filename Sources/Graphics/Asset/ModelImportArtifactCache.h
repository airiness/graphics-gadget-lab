#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/Asset/ModelImportArtifact.h"

#include <atomic>
#include <memory>
#include <unordered_map>

namespace gglab
{
	struct ModelImportArtifactCacheConfig
	{
		uint64_t m_BudgetBytes = 512ull * 1024ull * 1024ull;
	};

	struct ModelImportArtifactCacheStatistics
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

	class ModelImportArtifactCache final
	{
	public:
		explicit ModelImportArtifactCache(
			const ModelImportArtifactCacheConfig& config = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ModelImportArtifactCache);
		~ModelImportArtifactCache() = default;

		[[nodiscard]] ModelImportArtifactHandle Admit(
			ModelImportArtifactHandle artifact) noexcept;
		[[nodiscard]] ModelImportArtifactHandle Find(
			const ArtifactContentDigest& contentDigest) noexcept;
		[[nodiscard]] bool Contains(
			const ArtifactContentDigest& contentDigest) const noexcept;
		void Clear() noexcept;
		[[nodiscard]] ModelImportArtifactCacheStatistics GetStatistics() const noexcept;

	private:
		struct Entry
		{
			ModelImportArtifactHandle m_Artifact;
			uint64_t m_Bytes = 0;
			uint64_t m_LastAccessSerial = 0;
		};

		struct LiveState
		{
			std::atomic_uint64_t m_Bytes = 0;
		};

		void EvictToFit(uint64_t incomingBytes) noexcept;
		void EvictOne() noexcept;

		ModelImportArtifactCacheConfig m_Config{};
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
