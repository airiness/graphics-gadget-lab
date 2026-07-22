#include "Core/Precompiled.h"
#include "Graphics/Asset/TextureArtifactCache.h"

namespace gglab
{
	TextureArtifactCache::TextureArtifactCache(
		const TextureArtifactCacheConfig& config) noexcept :
		m_Core(config.m_BudgetBytes)
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
		return Admit(std::make_shared<const TextureArtifact>(std::move(artifact)));
	}

	TextureArtifactHandle TextureArtifactCache::Admit(
		TextureArtifactHandle artifact) noexcept
	{
		if (!artifact || !artifact->IsValid())
		{
			return {};
		}
		const ArtifactContentDigest contentDigest = artifact->m_ContentDigest;
		return m_Core.Admit(contentDigest, std::move(artifact));
	}

	TextureArtifactHandle TextureArtifactCache::Find(
		const ArtifactContentDigest& contentDigest) noexcept
	{
		return m_Core.Find(contentDigest);
	}

	bool TextureArtifactCache::Contains(
		const ArtifactContentDigest& contentDigest) const noexcept
	{
		return m_Core.Contains(contentDigest);
	}

	void TextureArtifactCache::Clear() noexcept
	{
		m_Core.Clear();
	}

	TextureArtifactCacheStatistics TextureArtifactCache::GetStatistics() const noexcept
	{
		return m_Core.GetStatistics();
	}
}
