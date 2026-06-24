#include "Core/Precompiled.h"
#include "Graphics/TextureAsset.h"

namespace gglab
{
	bool TextureAssetData::IsValid() const noexcept
	{
		return m_ResourceFormat != RHIFormat::Unknown &&
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
