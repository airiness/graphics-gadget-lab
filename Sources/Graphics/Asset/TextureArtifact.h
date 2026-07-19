#pragma once
#include "Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/TextureAsset.h"

#include <memory>

namespace gglab
{
	struct TextureArtifact
	{
		TextureAssetData m_Data;
		ArtifactContentDigest m_ContentDigest{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Data.IsValid() && m_ContentDigest.IsValid();
		}

		[[nodiscard]] uint64_t GetAllocatedBytes() const noexcept
		{
			return sizeof(TextureArtifact) +
				static_cast<uint64_t>(m_Data.m_Pixels.capacity()) * sizeof(std::byte) +
				static_cast<uint64_t>(m_Data.m_Subresources.capacity()) *
					sizeof(TextureAssetSubresource);
		}
	};

	using TextureArtifactHandle = std::shared_ptr<const TextureArtifact>;

	[[nodiscard]] ArtifactContentDigest ComputeTextureArtifactContentDigest(
		const TextureAssetData& textureData) noexcept;
}
