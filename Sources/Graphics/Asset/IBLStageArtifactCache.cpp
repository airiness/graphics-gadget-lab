#include "Graphics/Asset/IBLStageArtifactCache.h"

#include <utility>

namespace gglab
{
	IBLStageArtifactCache::IBLStageArtifactCache(const IBLStageArtifactCacheConfig& config) noexcept :
		m_Core(config.m_BudgetBytes)
	{
	}

	IBLStageArtifactHandle IBLStageArtifactCache::Admit(
		const DerivedDataKey& key, IBLStageArtifactHandle artifact) noexcept
	{
		if (!key.IsValid() || !artifact || !artifact->IsValid())
		{
			return {};
		}
		return m_Core.Admit(key, std::move(artifact));
	}

	IBLStageArtifactHandle IBLStageArtifactCache::Find(const DerivedDataKey& key) noexcept
	{
		return m_Core.Find(key);
	}

	bool IBLStageArtifactCache::Contains(const DerivedDataKey& key) const noexcept
	{
		return m_Core.Contains(key);
	}

	void IBLStageArtifactCache::Clear() noexcept
	{
		m_Core.Clear();
	}

	IBLStageArtifactCacheStatistics IBLStageArtifactCache::GetStatistics() const noexcept
	{
		return m_Core.GetStatistics();
	}
}
