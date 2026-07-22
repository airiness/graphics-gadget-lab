#include "Core/Precompiled.h"
#include "Graphics/Asset/ModelImportArtifactCache.h"

namespace gglab
{
	ModelImportArtifactCache::ModelImportArtifactCache(
		const ModelImportArtifactCacheConfig& config) noexcept :
		m_Core(config.m_BudgetBytes)
	{}

	ModelImportArtifactHandle ModelImportArtifactCache::Admit(
		ModelImportArtifactHandle artifact) noexcept
	{
		if (!artifact || !artifact->IsValid())
		{
			return {};
		}
		const ArtifactContentDigest contentDigest = artifact->m_ContentDigest;
		return m_Core.Admit(contentDigest, std::move(artifact));
	}

	ModelImportArtifactHandle ModelImportArtifactCache::Find(
		const ArtifactContentDigest& contentDigest) noexcept
	{
		return m_Core.Find(contentDigest);
	}

	bool ModelImportArtifactCache::Contains(
		const ArtifactContentDigest& contentDigest) const noexcept
	{
		return m_Core.Contains(contentDigest);
	}

	void ModelImportArtifactCache::Clear() noexcept
	{
		m_Core.Clear();
	}

	ModelImportArtifactCacheStatistics ModelImportArtifactCache::GetStatistics() const noexcept
	{
		return m_Core.GetStatistics();
	}
}
