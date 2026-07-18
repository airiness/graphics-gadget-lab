#include "Core/Precompiled.h"
#include "Graphics/Asset/TextureAsset.h"
#include "Core/Hash/KeyHash.h"

namespace gglab
{
	AssetContentFingerprint ComputeTextureContentFingerprint(
		const TextureAssetData& textureData,
		const TextureImportSettings& importSettings) noexcept
	{
		if (!textureData.IsValid())
		{
			return {};
		}

		uint64_t contentHash = FNV1a64::OffsetBasis;
		FNV1a64::MixValue(contentHash, textureData.m_ResourceFormat);
		FNV1a64::MixValue(contentHash, textureData.m_ViewFormat);
		FNV1a64::MixValue(contentHash, textureData.m_SrvDimension);
		FNV1a64::MixValue(contentHash, textureData.m_Extent.m_Width);
		FNV1a64::MixValue(contentHash, textureData.m_Extent.m_Height);
		FNV1a64::MixValue(contentHash, textureData.m_Extent.m_Depth);
		FNV1a64::MixValue(contentHash, textureData.m_ArraySize);
		FNV1a64::MixValue(contentHash, textureData.m_MipLevels);
		FNV1a64::MixValue(contentHash, textureData.m_ColorSpace);
		FNV1a64::MixValue(
			contentHash,
			static_cast<uint64_t>(textureData.m_Subresources.size()));
		for (const TextureAssetSubresource& subresource : textureData.m_Subresources)
		{
			FNV1a64::MixValue(contentHash, subresource.m_DataOffset);
			FNV1a64::MixValue(contentHash, subresource.m_DataSize);
			FNV1a64::MixValue(contentHash, subresource.m_RowPitch);
			FNV1a64::MixValue(contentHash, subresource.m_SlicePitch);
			FNV1a64::MixValue(contentHash, subresource.m_Width);
			FNV1a64::MixValue(contentHash, subresource.m_Height);
			FNV1a64::MixValue(contentHash, subresource.m_Depth);
			FNV1a64::MixValue(contentHash, subresource.m_MipLevel);
			FNV1a64::MixValue(contentHash, subresource.m_ArraySlice);
		}
		FNV1a64::MixBytes(
			contentHash,
			textureData.m_Pixels.data(),
			textureData.m_Pixels.size());

		uint64_t settingsHash = FNV1a64::OffsetBasis;
		FNV1a64::MixValue(settingsHash, importSettings.m_Semantic);
		FNV1a64::MixValue(settingsHash, importSettings.m_MipPolicy);

		return {
			.m_SourceContentHash = contentHash,
			.m_ImportSettingsHash = settingsHash,
			.m_DecoderVersion = TextureDecoderVersion,
		};
	}

	bool TextureAssetData::IsValid() const noexcept
	{
		const bool validViewDimension =
			m_SrvDimension == RHITextureViewDimension::Texture2D ||
			(m_SrvDimension == RHITextureViewDimension::Texture2DArray && m_ArraySize > 1) ||
			(m_SrvDimension == RHITextureViewDimension::TextureCube && m_ArraySize == CubemapFaceCount) ||
			(m_SrvDimension == RHITextureViewDimension::TextureCubeArray &&
				m_ArraySize >= CubemapFaceCount && (m_ArraySize % CubemapFaceCount) == 0);

		return validViewDimension &&
			m_ResourceFormat != RHIFormat::Unknown &&
			m_ViewFormat != RHIFormat::Unknown &&
			m_Extent.m_Width > 0 &&
			m_Extent.m_Height > 0 &&
			m_Extent.m_Depth > 0 &&
			m_ArraySize > 0 &&
			m_MipLevels > 0 &&
			!m_Pixels.empty() &&
			!m_Subresources.empty();
	}

	RHITextureUploadData TextureAssetData::MakeUploadData() const noexcept
	{
		RHITextureUploadData uploadData{};
		uploadData.m_Subresources.reserve(m_Subresources.size());

		for (const TextureAssetSubresource& subresource : m_Subresources)
		{
			if (subresource.m_DataOffset + subresource.m_DataSize > m_Pixels.size())
			{
				GGLAB_LOG_GRAPHICS_WARN("TextureAssetData::MakeUploadData skipped an out-of-range subresource.");
				continue;
			}

			uploadData.m_Subresources.push_back(
				{
					.m_Data = m_Pixels.data() + subresource.m_DataOffset,
					.m_RowPitch = subresource.m_RowPitch,
					.m_SlicePitch = subresource.m_SlicePitch,
				});
		}

		return uploadData;
	}
}
