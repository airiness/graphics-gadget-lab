#include "Core/Precompiled.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/Utility/DXGIFormatUtils.h"
#include "Graphics/Utility/TextureUtils.h"
#include "Core/HResult.h"
#include "Core/Utility/PathUtils.h"

#include <DirectXTex.h>

#include <cctype>
#include <cmath>

namespace gglab
{
	namespace
	{
		constexpr float MaxHalfFloatRadiance = 65000.0f;

		struct HdrSanitizationStats
		{
			float m_MaxFiniteChannel = 0.0f;
			uint64_t m_NonFiniteChannelCount = 0;
			uint64_t m_NegativeChannelCount = 0;
			uint64_t m_ClampedChannelCount = 0;
		};

		[[nodiscard]] HdrSanitizationStats SanitizeHdrForHalfFloat(
			DirectX::ScratchImage& scratchImage) noexcept
		{
			HdrSanitizationStats stats{};
			const DirectX::Image* images = scratchImage.GetImages();
			for (size_t imageIndex = 0; imageIndex < scratchImage.GetImageCount(); ++imageIndex)
			{
				const DirectX::Image& image = images[imageIndex];
				GGLAB_ASSERT(image.format == DXGI_FORMAT_R32G32B32A32_FLOAT);

				for (size_t y = 0; y < image.height; ++y)
				{
					auto* row = reinterpret_cast<float*>(image.pixels + y * image.rowPitch);
					for (size_t x = 0; x < image.width; ++x)
					{
						float* pixel = row + x * 4;
						for (size_t channel = 0; channel < 3; ++channel)
						{
							float& value = pixel[channel];
							if (!std::isfinite(value))
							{
								value = 0.0f;
								++stats.m_NonFiniteChannelCount;
								continue;
							}

							stats.m_MaxFiniteChannel = std::max(stats.m_MaxFiniteChannel, value);
							if (value < 0.0f)
							{
								value = 0.0f;
								++stats.m_NegativeChannelCount;
							}
							else if (value > MaxHalfFloatRadiance)
							{
								value = MaxHalfFloatRadiance;
								++stats.m_ClampedChannelCount;
							}
						}

						pixel[3] = std::isfinite(pixel[3]) ? std::clamp(pixel[3], 0.0f, 1.0f) : 1.0f;
					}
				}
			}
			return stats;
		}

		[[nodiscard]] RHIFormat ToTextureResourceFormat(DXGI_FORMAT format) noexcept
		{
			const DXGI_FORMAT typelessFormat = DirectX::MakeTypeless(format);
			DXGI_FORMAT resourceFormat = format;
			switch (typelessFormat)
			{
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			case DXGI_FORMAT_R32_TYPELESS:
				resourceFormat = typelessFormat;
				break;
			default:
				break;
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

		[[nodiscard]] DirectX::TEX_FILTER_FLAGS GetMipFilterFlags(
			TextureSemantic semantic) noexcept
		{
			DirectX::TEX_FILTER_FLAGS flags = DirectX::TEX_FILTER_FANT;
			if (GetTextureColorSpaceFromSemantic(semantic) == TextureColorSpace::SRGB)
			{
				flags |= DirectX::TEX_FILTER_SRGB;
			}
			return flags;
		}

		[[nodiscard]] bool RenormalizeNormalMipChain(
			DirectX::ScratchImage& mipChain,
			const std::filesystem::path& texPath) noexcept
		{
			DirectX::ScratchImage normalized;
			const HRESULT hr = DirectX::TransformImage(
				mipChain.GetImages(),
				mipChain.GetImageCount(),
				mipChain.GetMetadata(),
				[](DirectX::XMVECTOR* outPixels,
					const DirectX::XMVECTOR* inPixels,
					size_t width,
					size_t) noexcept
				{
					const DirectX::XMVECTOR scale = DirectX::XMVectorReplicate(2.0f);
					const DirectX::XMVECTOR bias = DirectX::XMVectorReplicate(-1.0f);
					const DirectX::XMVECTOR encodeScale = DirectX::XMVectorReplicate(0.5f);
					for (size_t x = 0; x < width; ++x)
					{
						DirectX::XMVECTOR normal = DirectX::XMVectorMultiplyAdd(inPixels[x], scale, bias);
						const float lengthSquared = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(normal));
						normal = lengthSquared > 1.0e-8f ?
							DirectX::XMVector3Normalize(normal) :
							DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
						DirectX::XMVECTOR encoded = DirectX::XMVectorMultiplyAdd(
							normal,
							encodeScale,
							encodeScale);
						outPixels[x] = DirectX::XMVectorSetW(encoded, DirectX::XMVectorGetW(inPixels[x]));
					}
				},
				normalized);
			if (FAILED(hr))
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"TextureLoader failed to renormalize normal-map mip chain '{}': {}",
					texPath.string(),
					FormatHResult(hr));
				return false;
			}

			mipChain = std::move(normalized);
			return true;
		}

		[[nodiscard]] bool GenerateMipChainIfNeeded(
			DirectX::ScratchImage& scratchImage,
			const std::filesystem::path& texPath,
			const TextureImportSettings& settings) noexcept
		{
			const DirectX::TexMetadata& metadata = scratchImage.GetMetadata();
			if (settings.m_MipPolicy != TextureMipPolicy::GenerateIfMissing ||
				metadata.mipLevels > 1 ||
				(metadata.width == 1 && metadata.height == 1))
			{
				return true;
			}

			DirectX::ScratchImage mipChain;
			const HRESULT hr = DirectX::GenerateMipMaps(
				scratchImage.GetImages(),
				scratchImage.GetImageCount(),
				metadata,
				GetMipFilterFlags(settings.m_Semantic),
				0,
				mipChain);
			if (FAILED(hr))
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"TextureLoader failed to generate mip chain '{}': {}",
					texPath.string(),
					FormatHResult(hr));
				return false;
			}

			if (settings.m_Semantic == TextureSemantic::Normal &&
				!RenormalizeNormalMipChain(mipChain, texPath))
			{
				return false;
			}

			scratchImage = std::move(mipChain);
			return true;
		}
	}

	TextureAssetData TextureLoader::LoadTextureData(
		const std::filesystem::path& texPath,
		const TextureImportSettings& settings) noexcept
	{
		const TextureColorSpace colorSpace = GetTextureColorSpaceFromSemantic(settings.m_Semantic);
		std::error_code errorCode;
		if (!std::filesystem::exists(texPath, errorCode) ||
			!std::filesystem::is_regular_file(texPath, errorCode))
		{
			GGLAB_LOG_GRAPHICS_ERROR("TextureLoader received an invalid texture file path: '{}'.",
				texPath.string());
			return {};
		}

		std::string extension = texPath.extension().string();
		std::ranges::transform(extension, extension.begin(),
			[](unsigned char value) noexcept
			{
				return static_cast<char>(std::tolower(value));
			});

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
		else if (extension == ".hdr")
		{
			hr = DirectX::LoadFromHDRFile(
				texPath.c_str(),
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

		if (extension == ".hdr")
		{
			DirectX::ScratchImage floatImage;
			DirectX::ScratchImage* hdrImage = &scratchImage;
			if (metadata.format != DXGI_FORMAT_R32G32B32A32_FLOAT)
			{
				hr = DirectX::Convert(
					scratchImage.GetImages(),
					scratchImage.GetImageCount(),
					metadata,
					DXGI_FORMAT_R32G32B32A32_FLOAT,
					DirectX::TEX_FILTER_DEFAULT,
					DirectX::TEX_THRESHOLD_DEFAULT,
					floatImage);
				if (FAILED(hr))
				{
					GGLAB_LOG_GRAPHICS_ERROR("TextureLoader failed to normalize HDR texture '{}': {}",
						texPath.string(),
						FormatHResult(hr));
					return {};
				}
				hdrImage = &floatImage;
			}

			const HdrSanitizationStats stats = SanitizeHdrForHalfFloat(*hdrImage);
			if (stats.m_NonFiniteChannelCount > 0 ||
				stats.m_NegativeChannelCount > 0 ||
				stats.m_ClampedChannelCount > 0)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"TextureLoader sanitized HDR '{}' for FP16 storage: maxFiniteChannel={}, nonFiniteChannels={}, negativeChannels={}, clampedChannels={}, clamp={}.",
					texPath.string(),
					stats.m_MaxFiniteChannel,
					stats.m_NonFiniteChannelCount,
					stats.m_NegativeChannelCount,
					stats.m_ClampedChannelCount,
					MaxHalfFloatRadiance);
			}

			DirectX::ScratchImage convertedImage;
			hr = DirectX::Convert(
				hdrImage->GetImages(),
				hdrImage->GetImageCount(),
				hdrImage->GetMetadata(),
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				DirectX::TEX_FILTER_DEFAULT,
				DirectX::TEX_THRESHOLD_DEFAULT,
				convertedImage);
			if (FAILED(hr))
			{
				GGLAB_LOG_GRAPHICS_ERROR("TextureLoader failed to convert HDR texture '{}': {}",
					texPath.string(),
					FormatHResult(hr));
				return {};
			}

			if (!GenerateMipChainIfNeeded(convertedImage, texPath, settings))
			{
				return {};
			}
			return ConvertScratchImage(convertedImage, TextureColorSpace::Linear);
		}

		if (!GenerateMipChainIfNeeded(scratchImage, texPath, settings))
		{
			return {};
		}
		return ConvertScratchImage(scratchImage, colorSpace);
	}

	TextureAssetData TextureLoader::LoadTextureData(
		const std::filesystem::path& texPath,
		TextureColorSpace colorSpace) noexcept
	{
		return LoadTextureData(
			texPath,
			TextureImportSettings
			{
				.m_Semantic = colorSpace == TextureColorSpace::SRGB ?
					TextureSemantic::GenericColor : TextureSemantic::GenericData,
				.m_MipPolicy = TextureMipPolicy::Preserve,
			});
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

	bool TextureLoader::SaveTextureDataToDDS(
		const TextureAssetData& textureData,
		const std::filesystem::path& path) noexcept
	{
		if (!textureData.IsValid() || !utils::CreateParentDirectoryIfNotExist(path))
		{
			GGLAB_LOG_GRAPHICS_ERROR("TextureLoader cannot save invalid texture data to '{}'.", path.string());
			return false;
		}

		const DXGI_FORMAT format = ToDXGIFormat(textureData.m_ViewFormat);
		if (format == DXGI_FORMAT_UNKNOWN)
		{
			GGLAB_LOG_GRAPHICS_ERROR("TextureLoader cannot save unsupported DDS format to '{}'.", path.string());
			return false;
		}

		std::vector<DirectX::Image> images;
		images.reserve(textureData.m_Subresources.size());
		for (const auto& subresource : textureData.m_Subresources)
		{
			if (subresource.m_DataOffset + subresource.m_DataSize > textureData.m_Pixels.size())
			{
				return false;
			}
			images.push_back(
				{
					.width = subresource.m_Width,
					.height = subresource.m_Height,
					.format = format,
					.rowPitch = static_cast<size_t>(subresource.m_RowPitch),
					.slicePitch = static_cast<size_t>(subresource.m_SlicePitch),
					.pixels = reinterpret_cast<uint8_t*>(const_cast<std::byte*>(
						textureData.m_Pixels.data() + subresource.m_DataOffset)),
				});
		}

		DirectX::TexMetadata metadata{};
		metadata.width = textureData.m_Extent.m_Width;
		metadata.height = textureData.m_Extent.m_Height;
		metadata.depth = std::max<uint32_t>(textureData.m_Extent.m_Depth, 1u);
		metadata.arraySize = textureData.m_ArraySize;
		metadata.mipLevels = textureData.m_MipLevels;
		metadata.format = format;
		metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
		if (textureData.m_ArraySize == CubemapFaceCount)
		{
			metadata.miscFlags = DirectX::TEX_MISC_TEXTURECUBE;
		}

		const HRESULT result = DirectX::SaveToDDSFile(
			images.data(),
			images.size(),
			metadata,
			DirectX::DDS_FLAGS_FORCE_DX10_EXT,
			path.c_str());
		if (FAILED(result))
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"TextureLoader failed to save DDS '{}': {}",
				path.string(),
				FormatHResult(result));
			return false;
		}
		return true;
	}
}
