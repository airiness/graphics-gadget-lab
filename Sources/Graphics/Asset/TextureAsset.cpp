#include "Graphics/Asset/TextureAsset.h"
#include "Core/Hash/KeyHash.h"
#include "Core/Log/LogMacros.h"
#include "Graphics/Asset/TextureAssetValidation.h"

#include <cstddef>
#include <cstdint>

namespace gglab
{
	AssetContentFingerprint ComputeTextureContentFingerprint(
		const TextureAssetData& textureData, const TextureImportSettings& importSettings) noexcept
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
		FNV1a64::MixValue(contentHash, static_cast<uint64_t>(textureData.m_Subresources.size()));
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
		FNV1a64::MixBytes(contentHash, textureData.m_Pixels.data(), textureData.m_Pixels.size());

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
		return ValidateTextureAssetStructure(*this).IsValid();
	}

	RHITextureUploadData TextureAssetData::MakeUploadData() const noexcept
	{
		const TextureStructureValidationResult validation = ValidateTextureAssetStructure(*this);
		if (!validation.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"TextureAssetData::MakeUploadData rejected invalid texture data: {}.",
				TextureStructureValidationErrorText(validation.m_Error));
			return {};
		}

		RHITextureUploadData uploadData{};
		uploadData.m_Subresources.resize(m_Subresources.size());

		for (const TextureAssetSubresource& subresource : m_Subresources)
		{
			const size_t index = static_cast<size_t>(subresource.m_ArraySlice) * m_MipLevels +
				subresource.m_MipLevel;
			uploadData.m_Subresources[index] = {
				.m_Data = m_Pixels.data() + subresource.m_DataOffset,
				.m_RowPitch = subresource.m_RowPitch,
				.m_SlicePitch = subresource.m_SlicePitch,
			};
		}

		return uploadData;
	}
}
