#include "Core/Precompiled.h"
#include "Graphics/Asset/TextureArtifact.h"
#include "Core/Hash/Sha256.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] ArtifactContentDigest ComputeValidatedTextureArtifactContentDigest(
			const TextureAssetData& textureData) noexcept
		{
			Sha256Builder builder;
			bool succeeded = builder.IsValid();
			succeeded &= builder.AddU32(static_cast<uint32_t>(textureData.m_ResourceFormat));
			succeeded &= builder.AddU32(static_cast<uint32_t>(textureData.m_ViewFormat));
			succeeded &= builder.AddU32(static_cast<uint32_t>(textureData.m_SrvDimension));
			succeeded &= builder.AddU32(textureData.m_Extent.m_Width);
			succeeded &= builder.AddU32(textureData.m_Extent.m_Height);
			succeeded &= builder.AddU32(textureData.m_Extent.m_Depth);
			succeeded &= builder.AddU16(textureData.m_ArraySize);
			succeeded &= builder.AddU16(textureData.m_MipLevels);
			succeeded &= builder.AddU32(static_cast<uint32_t>(textureData.m_ColorSpace));
			succeeded &= builder.AddU64(static_cast<uint64_t>(textureData.m_Subresources.size()));
			for (const TextureAssetSubresource& subresource : textureData.m_Subresources)
			{
				succeeded &= builder.AddU64(subresource.m_DataOffset);
				succeeded &= builder.AddU64(subresource.m_DataSize);
				succeeded &= builder.AddU64(subresource.m_RowPitch);
				succeeded &= builder.AddU64(subresource.m_SlicePitch);
				succeeded &= builder.AddU32(subresource.m_Width);
				succeeded &= builder.AddU32(subresource.m_Height);
				succeeded &= builder.AddU32(subresource.m_Depth);
				succeeded &= builder.AddU32(subresource.m_MipLevel);
				succeeded &= builder.AddU32(subresource.m_ArraySlice);
			}
			succeeded &= builder.AddBytes(textureData.m_Pixels);
			if (!succeeded)
			{
				GGLAB_LOG_GRAPHICS_ERROR("Failed to compute a SHA-256 texture artifact digest.");
				return {};
			}
			ArtifactContentDigest digest{};
			digest.m_Value = builder.Finish().m_Value;
			return digest;
		}
	}

	ArtifactContentDigest ComputeTextureArtifactContentDigest(
		const TextureAssetData& textureData) noexcept
	{
		return textureData.IsValid() ? ComputeValidatedTextureArtifactContentDigest(textureData)
			: ArtifactContentDigest{};
	}

	TextureArtifactBuildResult CreateTextureArtifact(
		TextureAssetData&& textureData, TextureAssetValidationLimits limits) noexcept
	{
		TextureArtifactBuildResult result{};
		const TextureStructureValidationResult validation =
			ValidateTextureAssetStructure(textureData, limits);
		if (!validation.IsValid())
		{
			result.m_Error = TextureArtifactBuildError::InvalidStructure;
			result.m_StructureError = validation.m_Error;
			return result;
		}

		const ArtifactContentDigest contentDigest =
			ComputeValidatedTextureArtifactContentDigest(textureData);
		if (!contentDigest.IsValid())
		{
			result.m_Error = TextureArtifactBuildError::DigestFailure;
			return result;
		}

		result.m_Artifact = {
			.m_Data = std::move(textureData),
			.m_ContentDigest = contentDigest,
		};
		return result;
	}
}
