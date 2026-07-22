#include "Core/Precompiled.h"
#include "Graphics/Asset/IBLStageArtifactCache.h"

namespace gglab
{
	IBLStageArtifactCache::IBLStageArtifactCache(
		const IBLStageArtifactCacheConfig& config) noexcept :
		m_Core(config.m_BudgetBytes)
	{}

	IBLStageArtifactHandle IBLStageArtifactCache::Admit(
		const DerivedDataKey& key,
		IBLStageArtifactHandle artifact) noexcept
	{
		if (!key.IsValid() || !artifact || !artifact->IsValid())
		{
			return {};
		}
		const uint64_t bytes = artifact->GetAllocatedBytes();
		return m_Core.Admit(
			key,
			{
				.m_Artifact = std::move(artifact),
				.m_PhysicalBytes = bytes,
			});
	}

	IBLStageArtifactHandle IBLStageArtifactCache::Find(
		const DerivedDataKey& key) noexcept
	{
		return m_Core.Find(key);
	}

	bool IBLStageArtifactCache::Contains(
		const DerivedDataKey& key) const noexcept
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
