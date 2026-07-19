#include "Core/Precompiled.h"
#include "Graphics/Asset/TextureArtifactCache.h"

namespace gglab
{
	TextureArtifactCache::TextureArtifactCache(
		const TextureArtifactCacheConfig& config) noexcept :
		m_Config(config),
		m_LiveState(std::make_shared<LiveState>())
	{}

	TextureArtifactHandle TextureArtifactCache::CreateAndAdmit(
		TextureAssetData&& data,
		ArtifactContentDigest contentDigest) noexcept
	{
		if (!data.IsValid())
		{
			return {};
		}
		if (!contentDigest.IsValid())
		{
			contentDigest = ComputeTextureArtifactContentDigest(data);
		}
		if (!contentDigest.IsValid())
		{
			return {};
		}
		return CreateAndAdmit(TextureArtifact{
			.m_Data = std::move(data),
			.m_ContentDigest = contentDigest,
		});
	}

	TextureArtifactHandle TextureArtifactCache::CreateAndAdmit(
		TextureArtifact&& artifact) noexcept
	{
		if (!artifact.IsValid())
		{
			return {};
		}
		const ArtifactContentDigest contentDigest = artifact.m_ContentDigest;

		if (auto existing = m_Entries.find(contentDigest); existing != m_Entries.end())
		{
			existing->second.m_LastAccessSerial = ++m_AccessSerial;
			return existing->second.m_Artifact;
		}

		auto artifactAllocation = std::make_unique<TextureArtifact>(std::move(artifact));
		const uint64_t bytes = artifactAllocation->GetAllocatedBytes();
		const std::shared_ptr<LiveState> liveState = m_LiveState;
		liveState->m_Bytes.fetch_add(bytes, std::memory_order_relaxed);
		TextureArtifactHandle handle(
			artifactAllocation.release(),
			[liveState, bytes](const TextureArtifact* value) noexcept
			{
				delete value;
				liveState->m_Bytes.fetch_sub(bytes, std::memory_order_relaxed);
			});

		if (bytes > m_Config.m_BudgetBytes)
		{
			++m_AdmissionRejectedCount;
			return handle;
		}
		EvictToFit(bytes);
		if (m_CachedBytes + bytes > m_Config.m_BudgetBytes)
		{
			++m_AdmissionRejectedCount;
			return handle;
		}

		m_Entries.emplace(contentDigest, Entry{
			.m_Artifact = handle,
			.m_Bytes = bytes,
			.m_LastAccessSerial = ++m_AccessSerial,
		});
		m_CachedBytes += bytes;
		++m_AdmissionCount;
		return handle;
	}

	TextureArtifactHandle TextureArtifactCache::Find(
		const ArtifactContentDigest& contentDigest) noexcept
	{
		const auto entry = m_Entries.find(contentDigest);
		if (entry == m_Entries.end())
		{
			++m_MissCount;
			return {};
		}
		entry->second.m_LastAccessSerial = ++m_AccessSerial;
		++m_HitCount;
		return entry->second.m_Artifact;
	}

	bool TextureArtifactCache::Contains(
		const ArtifactContentDigest& contentDigest) const noexcept
	{
		return m_Entries.contains(contentDigest);
	}

	void TextureArtifactCache::Clear() noexcept
	{
		while (!m_Entries.empty())
		{
			EvictOne();
		}
	}

	TextureArtifactCacheStatistics TextureArtifactCache::GetStatistics() const noexcept
	{
		const uint64_t liveBytes = m_LiveState->m_Bytes.load(std::memory_order_relaxed);
		return {
			.m_BudgetBytes = m_Config.m_BudgetBytes,
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

	void TextureArtifactCache::EvictToFit(uint64_t incomingBytes) noexcept
	{
		while (!m_Entries.empty() &&
			m_CachedBytes + incomingBytes > m_Config.m_BudgetBytes)
		{
			EvictOne();
		}
	}

	void TextureArtifactCache::EvictOne() noexcept
	{
		if (m_Entries.empty())
		{
			return;
		}
		const auto oldest = std::ranges::min_element(
			m_Entries,
			{},
			[](const auto& value) noexcept
			{
				return value.second.m_LastAccessSerial;
			});
		const uint64_t bytes = oldest->second.m_Bytes;
		GGLAB_ASSERT(bytes <= m_CachedBytes);
		m_CachedBytes = bytes <= m_CachedBytes ? m_CachedBytes - bytes : 0;
		++m_EvictionCount;
		m_EvictedBytes += bytes;
		m_Entries.erase(oldest);
	}
}
