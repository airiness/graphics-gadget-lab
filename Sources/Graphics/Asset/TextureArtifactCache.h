#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/Asset/TextureArtifact.h"

#include <atomic>
#include <memory>
#include <unordered_map>

namespace gglab
{
	struct TextureArtifactCacheConfig
	{
		uint64_t m_BudgetBytes = 512ull * 1024ull * 1024ull;
	};

	struct TextureArtifactCacheStatistics
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

	class TextureArtifactCache final
	{
	public:
		explicit TextureArtifactCache(
			const TextureArtifactCacheConfig& config = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TextureArtifactCache);
		~TextureArtifactCache() = default;

		[[nodiscard]] TextureArtifactHandle CreateAndAdmit(
			TextureAssetData&& data,
			ArtifactContentDigest contentDigest = {}) noexcept;
		[[nodiscard]] TextureArtifactHandle CreateAndAdmit(
			TextureArtifact&& artifact) noexcept;
		[[nodiscard]] TextureArtifactHandle Find(
			const ArtifactContentDigest& contentDigest) noexcept;
		[[nodiscard]] bool Contains(
			const ArtifactContentDigest& contentDigest) const noexcept;
		void Clear() noexcept;
		[[nodiscard]] TextureArtifactCacheStatistics GetStatistics() const noexcept;

	private:
		struct Entry
		{
			TextureArtifactHandle m_Artifact;
			uint64_t m_Bytes = 0;
			uint64_t m_LastAccessSerial = 0;
		};

		struct LiveState
		{
			std::atomic_uint64_t m_Bytes = 0;
		};

		void EvictToFit(uint64_t incomingBytes) noexcept;
		void EvictOne() noexcept;

		TextureArtifactCacheConfig m_Config{};
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
