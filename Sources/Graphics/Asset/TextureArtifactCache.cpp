#include "Core/Precompiled.h"
#include "Graphics/Asset/TextureArtifactCache.h"

namespace gglab
{
	TextureArtifactCache::TextureArtifactCache(
		const TextureArtifactCacheConfig& config) noexcept :
		m_Core(config.m_BudgetBytes)
	{}

	TextureArtifactHandle TextureArtifactCache::CreateAndAdmit(
		TextureAssetData&& data) noexcept
	{
		TextureArtifactBuildResult built = CreateTextureArtifact(std::move(data));
		if (!built.Succeeded())
		{
			return {};
		}
		return Admit(std::make_shared<const TextureArtifact>(
			std::move(built.m_Artifact)));
	}

	TextureArtifactHandle TextureArtifactCache::Admit(
		TextureArtifactHandle artifact) noexcept
	{
		if (!artifact || !artifact->IsValid())
		{
			return {};
		}
		const ArtifactContentDigest contentDigest = artifact->m_ContentDigest;
		std::scoped_lock lock(m_Mutex);
		return m_Core.Admit(contentDigest, std::move(artifact));
	}

	TextureArtifactHandle TextureArtifactCache::Find(
		const ArtifactContentDigest& contentDigest) noexcept
	{
		std::scoped_lock lock(m_Mutex);
		return m_Core.Find(contentDigest);
	}

	bool TextureArtifactCache::Contains(
		const ArtifactContentDigest& contentDigest) const noexcept
	{
		std::scoped_lock lock(m_Mutex);
		return m_Core.Contains(contentDigest);
	}

	void TextureArtifactCache::Clear() noexcept
	{
		std::scoped_lock lock(m_Mutex);
		m_Core.Clear();
	}

	TextureArtifactCacheStatistics TextureArtifactCache::GetStatistics() const noexcept
	{
		std::scoped_lock lock(m_Mutex);
		return m_Core.GetStatistics();
	}
}
