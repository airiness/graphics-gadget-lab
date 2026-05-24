#include "Core/Precompiled.h"
#include "Graphics/TextureRegistry.h"
#include "Graphics/TransferManager.h"
#include "Graphics/DX12/DX12Device.h"
#include "Graphics/DX12/DX12Texture.h"
#include "Graphics/DX12/DX12CommandList.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/Utility/TextureUtils.h"
#include "Core/HResult.h"
#include "Core/Utility/PathUtils.h"
#include "Core/Utility/TypeUtils.h"

namespace gglab
{
	TextureRegistry::TextureRegistry(const CreateInfo& createInfo) noexcept :
		m_DX12Device(createInfo.m_DX12Device),
		m_TransferManager(createInfo.m_TransferManager),
		m_DescriptorManager(createInfo.m_DescriptorManager)
	{
		GGLAB_ASSERT_MSG(m_DX12Device != nullptr, "DX12Device is null!");
		GGLAB_ASSERT_MSG(m_TransferManager != nullptr, "TransferManager is null!");
		GGLAB_ASSERT_MSG(m_DescriptorManager != nullptr, "DX12DescriptorManager is null!");
	}

	void TextureRegistry::InitializeReservedTextures() noexcept
	{
		const auto makeScratchImage = [](uint32_t width, uint32_t height, auto&& pixelFunc) -> DirectX::ScratchImage
			{
				GGLAB_ASSERT(width > 0 && height > 0);
				constexpr size_t formatBytes = 4;

				DirectX::ScratchImage scratchImage;
				HRESULT hr = scratchImage.Initialize2D(
					DXGI_FORMAT_R8G8B8A8_UNORM,
					width,
					height,
					1,
					1,
					DirectX::CP_FLAGS_NONE);
				GGLAB_HR(hr);

				auto* image = scratchImage.GetImage(0, 0, 0);
				GGLAB_ASSERT(image != nullptr && image->pixels != nullptr);

				for (uint32_t y = 0; y < height; ++y)
				{
					uint8_t* row = image->pixels + static_cast<size_t>(y) * image->rowPitch;
					for (uint32_t x = 0; x < width; ++x)
					{
						const auto color = pixelFunc(x, y);
						uint8_t* pixel = row + static_cast<size_t>(x) * formatBytes;
						pixel[0] = color[0];
						pixel[1] = color[1];
						pixel[2] = color[2];
						pixel[3] = color[3];
					}
				}
				return scratchImage;
			};

		const auto makeUploadData = [&, this](ReservedTextureIDIndex idIndex,
			const char* texName,
			TextureSemantic semantic,
			DirectX::ScratchImage&& image)
			{
				const auto& id = ToTextureId(idIndex);
				CreateTextureEntry(id, texName);

				return MakeTextureUploadData(id, std::move(image), semantic);
			};

		std::vector<TextureUploadData> uploads;
		uploads.reserve(utils::ToIndex(ReservedTextureIDIndex::Count));

		uploads.emplace_back(makeUploadData(
			ReservedTextureIDIndex::BaseColorWhite,
			"BaseColorWhite",
			TextureSemantic::BaseColor,
			makeScratchImage(1, 1,
				[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
				{
					return { 255, 255, 255, 255 };
				})));

		uploads.emplace_back(makeUploadData(
			ReservedTextureIDIndex::MissingTextureChecker,
			"MissingTextureChecker",
			TextureSemantic::BaseColor,
			makeScratchImage(64, 64,
				[](uint32_t x, uint32_t y) -> std::array<uint8_t, 4>
				{
					const uint32_t tile = 8;
					const bool isPurple = ((x / tile) + (y / tile)) & 1;
					return isPurple ?
						std::array<uint8_t, 4>{ 255, 0, 255, 255 } :
						std::array<uint8_t, 4>{ 0, 0, 0, 255 };
				})));

		uploads.emplace_back(makeUploadData(
			ReservedTextureIDIndex::NormalFlat,
			"NormalFlat",
			TextureSemantic::Normal,
			makeScratchImage(1, 1,
				[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
				{
					return { 128, 128, 255, 255 };
				})));

		uploads.emplace_back(makeUploadData(
			ReservedTextureIDIndex::DefaultMetallicRoughness,
			"DefaultMetallicRoughness",
			TextureSemantic::MetallicRoughness,
			makeScratchImage(1, 1,
				[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
				{
					// glTF: G=Roughness(1), B=Metallic(0). R unused.
					return { 0, 255, 0, 255 };
				})));

		uploads.emplace_back(makeUploadData(
			ReservedTextureIDIndex::OcclusionWhite,
			"OcclusionWhite",
			TextureSemantic::Occlusion,
			makeScratchImage(1, 1,
				[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
				{
					return { 255, 255, 255, 255 };
				})));

		uploads.emplace_back(makeUploadData(
			ReservedTextureIDIndex::EmissiveBlack,
			"EmissiveBlack",
			TextureSemantic::Emissive,
			makeScratchImage(1, 1,
				[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
				{
					return { 0, 0, 0, 255 };
				})));

		uploads.emplace_back(makeUploadData(
			ReservedTextureIDIndex::ErrorRed,
			"ErrorRed",
			TextureSemantic::BaseColor,
			makeScratchImage(1, 1,
				[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
				{
					return { 255, 0, 0, 255 };
				})));

		uploads.emplace_back(makeUploadData(
			ReservedTextureIDIndex::UVTest,
			"UVTest",
			TextureSemantic::UVTest,
			makeScratchImage(256, 256,
				[](uint32_t x, uint32_t y) -> std::array<uint8_t, 4>
				{
					uint8_t r = static_cast<uint8_t>(x);
					uint8_t g = static_cast<uint8_t>(y);
					uint8_t b = 0;

					const uint32_t grid = 32;
					if ((grid != 0) && ((x % grid) == 0 || (y % grid) == 0))
					{
						r = g = b = 0;
					}

					return { r, g, b, 255 };
				})));

		{
			auto texPath = utils::Canonical(std::filesystem::path("Assets/Textures/UVTest1K.png"));
			const auto id = ToTextureId(ReservedTextureIDIndex::UVTestTexture1K);
			CreateTextureEntry(id, "UVTestTexture1K");
			uploads.emplace_back(MakeTextureUploadData(id, texPath, TextureSemantic::UVTest));
			m_TextureContainer.m_PathIDMap[texPath] = id;
		}

		{
			auto texPath = utils::Canonical(std::filesystem::path("Assets/Textures/UVTest4K.png"));
			const auto id = ToTextureId(ReservedTextureIDIndex::UVTestTexture4K);
			CreateTextureEntry(id, "UVTestTexture4K");
			uploads.emplace_back(MakeTextureUploadData(id, texPath, TextureSemantic::UVTest));
			m_TextureContainer.m_PathIDMap[texPath] = id;
		}

		if (!uploads.empty())
		{
			auto batch = m_TransferManager->BeginBatch();
			auto* copyContext = m_TransferManager->GetCopyContext();
			for (const auto& data : uploads)
			{
				UploadTexture(data, *copyContext);
			}
			batch.Submit(true);
		}
	}

	void TextureRegistry::Finalize(const DX12FencePoint& fencePoint) noexcept
	{
		for (const auto& texture : m_TextureContainer.m_TextureIDMap | std::views::values)
		{
			if (texture->m_DescriptorId.IsValid())
			{
				m_DescriptorManager->RetireBindlessSrvId(texture->m_DescriptorId, fencePoint);
				texture->m_DescriptorId = {};
			}
			texture->m_Texture.reset();
		}
	}

	TextureID TextureRegistry::LoadTexture(const std::filesystem::path& path, TextureSemantic semantic) noexcept
	{
		const auto canonicalPath = utils::Canonical(path);

		auto textureId = FindTexture(canonicalPath);
		if (textureId.IsValid())
		{
			if (const auto* tex = GetTexture(textureId); tex && tex->m_IsUploaded)
			{
				if (tex->m_Semantic != TextureSemantic::Unknown && tex->m_Semantic != semantic)
				{
					GGLAB_LOG_GRAPHICS_WARN("Texture '{}' is already loaded with different semantic (existing: {}, requested: {}).",
						canonicalPath.string(),
						utils::ToUnderlying(tex->m_Semantic),
						utils::ToUnderlying(semantic));
				}
			}
			return textureId;
		}

		textureId = CreateTexture(canonicalPath);

		auto texUploadData = MakeTextureUploadData(textureId, canonicalPath, semantic);
		auto batch = m_TransferManager->BeginBatch();
		auto* copyContext = m_TransferManager->GetCopyContext();
		UploadTexture(texUploadData, *copyContext);
		batch.Submit(true);

		return textureId;
	}

	Texture* TextureRegistry::GetTexture(TextureID textureId) noexcept
	{
		return const_cast<Texture*>(std::as_const(*this).GetTexture(textureId));
	}

	const Texture* TextureRegistry::GetTexture(TextureID textureId) const noexcept
	{
		auto iterator = m_TextureContainer.m_TextureIDMap.find(textureId);
		if (iterator != m_TextureContainer.m_TextureIDMap.end())
		{
			return iterator->second.get();
		}
		return nullptr;
	}

	uint32_t TextureRegistry::ResolveSrvIndex(TextureID textureId, ReservedTextureIDIndex fallback) const noexcept
	{
		if (const auto* texture = GetTexture(textureId); texture != nullptr)
		{
			if (texture->m_IsUploaded && texture->m_DescriptorId.IsValid())
			{
				return m_DescriptorManager->BindlessSrvIdToGlobalIndex(texture->m_DescriptorId);
			}
		}

		const auto fallbackId = ToTextureId(fallback);
		const auto* fallbackTexture = GetTexture(fallbackId);

		GGLAB_ASSERT(fallbackTexture != nullptr && fallbackTexture->m_DescriptorId.IsValid());

		return m_DescriptorManager->BindlessSrvIdToGlobalIndex(fallbackTexture->m_DescriptorId);
	}

	TextureID TextureRegistry::CreateTexture(const std::filesystem::path& canonicalPath) noexcept
	{
		const auto textureId = m_TextureIdCounter.Acquire();
		auto pathIdPair = m_TextureContainer.m_PathIDMap.emplace(canonicalPath, textureId);
		GGLAB_ASSERT_MSG(pathIdPair.second == true, "TextureRegistry: emplace path and TextureID pair failed.");

		auto idTexPair = m_TextureContainer.m_TextureIDMap.emplace(textureId, std::make_unique<Texture>());
		GGLAB_ASSERT_MSG(idTexPair.second == true, "TextureRegistry: emplace TextureID and Texture pair failed.");

		return textureId;
	}

	TextureID TextureRegistry::FindTexture(const std::filesystem::path& canonicalPath) const noexcept
	{
		auto& texNameIdsMap = m_TextureContainer.m_PathIDMap;
		auto iterator = texNameIdsMap.find(canonicalPath);
		if (iterator != texNameIdsMap.end())
		{
			return iterator->second;
		}
		return InvalidTextureID;
	}

	TextureRegistry::TextureUploadData TextureRegistry::MakeTextureUploadData(TextureID textureId,
		const std::filesystem::path& canonicalPath, TextureSemantic semantic) noexcept
	{
		TextureUploadData uploadData{};
		uploadData.m_TextureId = textureId;
		uploadData.m_Semantic = semantic;
		uploadData.m_ColorSpace = GetTextureColorSpaceFromSemantic(semantic);
		uploadData.m_ScratchImage = LoadTextureScratchImage(canonicalPath, uploadData.m_ColorSpace);
		return uploadData;
	}

	TextureRegistry::TextureUploadData TextureRegistry::MakeTextureUploadData(TextureID textureId,
		DirectX::ScratchImage&& scratchImage, TextureSemantic semantic) noexcept
	{
		TextureUploadData uploadData{};
		uploadData.m_TextureId = textureId;
		uploadData.m_Semantic = semantic;
		uploadData.m_ColorSpace = GetTextureColorSpaceFromSemantic(semantic);
		uploadData.m_ScratchImage = std::move(scratchImage);
		return uploadData;
	}

	void TextureRegistry::UploadTexture(const TextureUploadData& uploadData, CopyContext& copyContext) noexcept
	{
		auto* texture = GetTexture(uploadData.m_TextureId);
		GGLAB_ASSERT_MSG(texture != nullptr, "TextureRegistry::UploadTexture: invalid TextureID.");

		const auto& texMedaData = uploadData.m_ScratchImage.GetMetadata();

		auto resourceFormat = DirectX::MakeTypeless(texMedaData.format);
		if (resourceFormat == DXGI_FORMAT_UNKNOWN)
		{
			resourceFormat = texMedaData.format;
		}

		auto srvFormat = uploadData.m_ColorSpace == TextureColorSpace::SRGB ?
			DirectX::MakeSRGB(texMedaData.format) :
			DirectX::MakeLinear(texMedaData.format);
		if (srvFormat == DXGI_FORMAT_UNKNOWN)
		{
			srvFormat = texMedaData.format;
		}

		texture->m_Texture = std::make_unique<DX12Texture>();
		texture->m_Texture->Create(DX12Texture::AssetTextureCreateInfo(m_DX12Device->GetMemAllocator(),
			CD3DX12_RESOURCE_DESC::Tex2D(resourceFormat,
				static_cast<UINT64>(texMedaData.width),
				static_cast<UINT>(texMedaData.height),
				static_cast<UINT16>(texMedaData.arraySize),
				static_cast<UINT16>(texMedaData.mipLevels))));

		const auto imageCount = uploadData.m_ScratchImage.GetImageCount();
		std::vector<D3D12_SUBRESOURCE_DATA> subResourceDatas(imageCount);

		const auto& images = uploadData.m_ScratchImage.GetImages();
		for (int32_t imageIndex = 0; imageIndex < static_cast<int32_t>(imageCount); ++imageIndex)
		{
			subResourceDatas[imageIndex].pData = images[imageIndex].pixels;
			subResourceDatas[imageIndex].RowPitch = images[imageIndex].rowPitch;
			subResourceDatas[imageIndex].SlicePitch = images[imageIndex].slicePitch;
		}

		copyContext.UploadResource(subResourceDatas, texture->m_Texture.get());

		auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
			texture->m_Texture->Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		copyContext.GetCommandList()->Get()->ResourceBarrier(1, &transition);

		TextureSrvCreateInfo srvInfo{};
		srvInfo.m_Format = srvFormat;
		srvInfo.m_Dimension = D3D12_SRV_DIMENSION_TEXTURE2D;

		const auto srvDescriptorId = m_DescriptorManager->CreateBindlessSrv(texture->m_Texture.get(), srvInfo);
		texture->m_DescriptorId = srvDescriptorId;
		texture->m_Semantic = uploadData.m_Semantic;
		texture->m_IsUploaded = true;
	}

	void TextureRegistry::CreateTextureEntry(TextureID id, const char* texName) noexcept
	{
		if (GetTexture(id) == nullptr)
		{
			auto [iterator, result] = m_TextureContainer.m_TextureIDMap.emplace(id, std::make_unique<Texture>());
			if (result)
			{
				auto& texture = iterator->second;
				texture->m_Id = id;
				texture->m_Name = StringID(texName);
				texture->m_IsUploaded = false;
				texture->m_DescriptorId = {};
				texture->m_Texture.reset();
			}
		}
	}

	DirectX::ScratchImage TextureRegistry::LoadTextureScratchImage(
		const std::filesystem::path& texPath, TextureColorSpace colorSpace) noexcept
	{
		GGLAB_ASSERT_MSG(std::filesystem::exists(texPath), "Invalid texture file path.");

		const auto extension = texPath.extension().string();

		DirectX::TexMetadata metaData;
		DirectX::ScratchImage scratchImage;

		if (extension == ".dds")
		{
			GGLAB_HR(LoadFromDDSFile(texPath.c_str(),
				DirectX::DDS_FLAGS::DDS_FLAGS_FORCE_RGB,
				&metaData,
				scratchImage));
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

			GGLAB_HR(LoadFromWICFile(texPath.c_str(),
				wicFlags,
				&metaData,
				scratchImage));
		}

		return scratchImage;
	}
}
