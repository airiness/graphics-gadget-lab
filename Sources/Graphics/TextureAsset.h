#pragma once
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHITexture.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gglab
{
	struct TextureAssetSubresource
	{
		uint64_t m_DataOffset = 0;
		uint64_t m_DataSize = 0;
		uint64_t m_RowPitch = 0;
		uint64_t m_SlicePitch = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_Depth = 1;
		uint32_t m_MipLevel = 0;
		uint32_t m_ArraySlice = 0;
	};

	struct TextureAssetData
	{
		RHIFormat m_ResourceFormat = RHIFormat::Unknown;
		RHIFormat m_ViewFormat = RHIFormat::Unknown;
		RHIExtent3D m_Extent{};
		uint16_t m_ArraySize = 1;
		uint16_t m_MipLevels = 1;
		TextureColorSpace m_ColorSpace = TextureColorSpace::Linear;
		std::vector<std::byte> m_Pixels;
		std::vector<TextureAssetSubresource> m_Subresources;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] RHITextureUploadData MakeUploadData() const noexcept;
	};
}
