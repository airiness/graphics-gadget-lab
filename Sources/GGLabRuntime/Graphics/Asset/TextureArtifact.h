#pragma once
#include "GGLabRuntime/Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/TextureAssetValidation.h"

#include <cstddef>
#include <cstdint>
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

	enum class TextureArtifactBuildError : uint8_t
	{
		None,
		InvalidStructure,
		DigestFailure,
	};

	struct TextureArtifactBuildResult
	{
		TextureArtifact m_Artifact;
		TextureArtifactBuildError m_Error = TextureArtifactBuildError::None;
		TextureStructureValidationError m_StructureError = TextureStructureValidationError::None;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return m_Error == TextureArtifactBuildError::None && m_Artifact.IsValid();
		}
	};

	[[nodiscard]] ArtifactContentDigest ComputeTextureArtifactContentDigest(
		const TextureAssetData& textureData) noexcept;
	[[nodiscard]] TextureArtifactBuildResult CreateTextureArtifact(
		TextureAssetData&& textureData, TextureAssetValidationLimits limits = {}) noexcept;
}
