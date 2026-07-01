#include "Core/Precompiled.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/RHI/DX12/Utility/DX12FormatUtils.h"
#include "Core/HResult.h"

#include <DirectXTex.h>

namespace gglab
{
	namespace
	{
		[[nodiscard]] RHIFormat ToTextureResourceFormat(DXGI_FORMAT format) noexcept
		{
			DXGI_FORMAT resourceFormat = DirectX::MakeTypeless(format);
			if (resourceFormat == DXGI_FORMAT_UNKNOWN)
			{
				resourceFormat = format;
			}

			return ToRHIFormat(resourceFormat);
		}

		[[nodiscard]] RHIFormat ToTextureViewFormat(DXGI_FORMAT format, TextureColorSpace colorSpace) noexcept
		{
			DXGI_FORMAT viewFormat = colorSpace == TextureColorSpace::SRGB ?
				DirectX::MakeSRGB(format) :
				DirectX::MakeLinear(format);
			if (viewFormat == DXGI_FORMAT_UNKNOWN)
			{
				viewFormat = format;
			}

			return ToRHIFormat(viewFormat);
		}

		[[nodiscard]] TextureAssetData ConvertScratchImage(
			const DirectX::ScratchImage& scratchImage,
			TextureColorSpace colorSpace) noexcept
		{
			TextureAssetData result{};

			const DirectX::TexMetadata& metadata = scratchImage.GetMetadata();
			result.m_ResourceFormat = ToTextureResourceFormat(metadata.format);
			result.m_ViewFormat = ToTextureViewFormat(metadata.format, colorSpace);
			result.m_Extent =
			{
				.m_Width = static_cast<uint32_t>(metadata.width),
				.m_Height = static_cast<uint32_t>(metadata.height),
				.m_Depth = static_cast<uint32_t>(std::max<size_t>(metadata.depth, 1)),
			};
			result.m_ArraySize = static_cast<uint16_t>(metadata.arraySize);
			result.m_MipLevels = static_cast<uint16_t>(metadata.mipLevels);
			result.m_ColorSpace = colorSpace;

			if (result.m_ResourceFormat == RHIFormat::Unknown ||
				result.m_ViewFormat == RHIFormat::Unknown ||
				result.m_Extent.m_Width == 0 ||
				result.m_Extent.m_Height == 0 ||
				result.m_ArraySize == 0 ||
				result.m_MipLevels == 0)
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"TextureLoader: unsupported texture metadata format={} size={}x{} array={} mips={}.",
					static_cast<uint32_t>(metadata.format),
					metadata.width,
					metadata.height,
					metadata.arraySize,
					metadata.mipLevels);
				return {};
			}

			const size_t imageCount = scratchImage.GetImageCount();
			const DirectX::Image* images = scratchImage.GetImages();
			if (imageCount == 0 || images == nullptr)
			{
				GGLAB_LOG_GRAPHICS_ERROR("TextureLoader: decoded texture has no image data.");
				return {};
			}

			result.m_Subresources.reserve(imageCount);
			for (size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
			{
				const DirectX::Image& image = images[imageIndex];
				if (image.pixels == nullptr || image.slicePitch == 0)
				{
					GGLAB_LOG_GRAPHICS_ERROR("TextureLoader: decoded texture contains an empty subresource.");
					return {};
				}

				const uint64_t dataOffset = static_cast<uint64_t>(result.m_Pixels.size());
				const uint64_t dataSize = static_cast<uint64_t>(image.slicePitch);
				const auto* begin = reinterpret_cast<const std::byte*>(image.pixels);
				result.m_Pixels.insert(result.m_Pixels.end(), begin, begin + dataSize);

				const uint32_t mipLevel = static_cast<uint32_t>(imageIndex % result.m_MipLevels);
				const uint32_t arraySlice = static_cast<uint32_t>(imageIndex / result.m_MipLevels);
				result.m_Subresources.push_back(
					{
						.m_DataOffset = dataOffset,
						.m_DataSize = dataSize,
						.m_RowPitch = static_cast<uint64_t>(image.rowPitch),
						.m_SlicePitch = static_cast<uint64_t>(image.slicePitch),
						.m_Width = static_cast<uint32_t>(image.width),
						.m_Height = static_cast<uint32_t>(image.height),
						.m_Depth = 1,
						.m_MipLevel = mipLevel,
						.m_ArraySlice = arraySlice,
					});
			}

			return result;
		}
	}

	TextureAssetData TextureLoader::LoadTextureData(
		const std::filesystem::path& texPath,
		TextureColorSpace colorSpace) noexcept
	{
		std::error_code errorCode;
		if (!std::filesystem::exists(texPath, errorCode) ||
			!std::filesystem::is_regular_file(texPath, errorCode))
		{
			GGLAB_LOG_GRAPHICS_ERROR("TextureLoader received an invalid texture file path: '{}'.",
				texPath.string());
			return {};
		}

		const std::string extension = texPath.extension().string();

		DirectX::TexMetadata metadata;
		DirectX::ScratchImage scratchImage;
		HRESULT hr = S_OK;

		if (extension == ".dds")
		{
			hr = DirectX::LoadFromDDSFile(
				texPath.c_str(),
				DirectX::DDS_FLAGS::DDS_FLAGS_FORCE_RGB,
				&metadata,
				scratchImage);
		}
		else
		{
			DirectX::WIC_FLAGS wicFlags = DirectX::WIC_FLAGS::WIC_FLAGS_FORCE_RGB;
			if (colorSpace == TextureColorSpace::SRGB)
			{
				wicFlags |= DirectX::WIC_FLAGS::WIC_FLAGS_FORCE_SRGB;
			}
			else
			{
				wicFlags |= DirectX::WIC_FLAGS::WIC_FLAGS_IGNORE_SRGB;
			}

			hr = DirectX::LoadFromWICFile(
				texPath.c_str(),
				wicFlags,
				&metadata,
				scratchImage);
		}

		if (FAILED(hr))
		{
			GGLAB_LOG_GRAPHICS_ERROR("TextureLoader failed to decode texture '{}': {}",
				texPath.string(),
				FormatHResult(hr));
			return {};
		}

		return ConvertScratchImage(scratchImage, colorSpace);
	}

	TextureAssetData TextureLoader::MakeTexture2DRgba8(
		uint32_t width,
		uint32_t height,
		std::span<const uint8_t> pixels,
		TextureColorSpace colorSpace) noexcept
	{
		TextureAssetData result{};
		if (width == 0 || height == 0 || pixels.size_bytes() != static_cast<size_t>(width) * height * 4)
		{
			GGLAB_LOG_GRAPHICS_ERROR("TextureLoader::MakeTexture2DRgba8 received invalid texture data.");
			return result;
		}

		result.m_ResourceFormat = RHIFormat::R8G8B8A8Typeless;
		result.m_ViewFormat = colorSpace == TextureColorSpace::SRGB ?
			RHIFormat::R8G8B8A8UnormSrgb :
			RHIFormat::R8G8B8A8Unorm;
		result.m_Extent =
		{
			.m_Width = width,
			.m_Height = height,
			.m_Depth = 1,
		};
		result.m_ArraySize = 1;
		result.m_MipLevels = 1;
		result.m_ColorSpace = colorSpace;

		result.m_Pixels.resize(pixels.size_bytes());
		std::memcpy(result.m_Pixels.data(), pixels.data(), pixels.size_bytes());

		const uint64_t rowPitch = static_cast<uint64_t>(width) * 4;
		const uint64_t slicePitch = rowPitch * height;
		result.m_Subresources.push_back(
			{
				.m_DataOffset = 0,
				.m_DataSize = slicePitch,
				.m_RowPitch = rowPitch,
				.m_SlicePitch = slicePitch,
				.m_Width = width,
				.m_Height = height,
				.m_Depth = 1,
				.m_MipLevel = 0,
				.m_ArraySlice = 0,
			});

		return result;
	}
}
