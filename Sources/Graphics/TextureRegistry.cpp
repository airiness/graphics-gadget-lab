#include "Core/Precompiled.h"
#include "Graphics/TextureRegistry.h"
#include "Graphics/TransferManager.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/Utility/TextureUtils.h"
#include "Core/Utility/PathUtils.h"
#include "Core/Utility/TypeUtils.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] constexpr std::string_view TextureSemanticDebugText(
			TextureSemantic semantic) noexcept
		{
			switch (semantic)
			{
			case TextureSemantic::Unknown: return "Unknown";
			case TextureSemantic::BaseColor: return "BaseColor";
			case TextureSemantic::MetallicRoughness: return "MetallicRoughness";
			case TextureSemantic::Normal: return "Normal";
			case TextureSemantic::Occlusion: return "Occlusion";
			case TextureSemantic::Emissive: return "Emissive";
			case TextureSemantic::Environment: return "Environment";
			case TextureSemantic::UVTest: return "UVTest";
			case TextureSemantic::GenericColor: return "GenericColor";
			case TextureSemantic::GenericData: return "GenericData";
			}
			return "Unknown";
		}

		[[nodiscard]] std::string TextureDebugSourcePath(
			const std::filesystem::path& sourcePath)
		{
			if (sourcePath.empty())
			{
				return {};
			}

			const std::string normalized = sourcePath.generic_string();
			const size_t assetsOffset = normalized.find("Assets/");
			if (assetsOffset != std::string::npos)
			{
				return normalized.substr(assetsOffset);
			}

			return std::format(
				"External/{}#{:08X}",
				sourcePath.filename().generic_string(),
				static_cast<uint32_t>(Crc64(normalized)));
		}
	}

	TextureRegistry::TextureRegistry(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_TransferManager(createInfo.m_TransferManager)
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "RHIDevice is null!");
		GGLAB_ASSERT_MSG(m_TransferManager != nullptr, "TransferManager is null!");
	}

	void TextureRegistry::InitializeReservedTextures() noexcept
	{
		const auto makeTextureData = [](uint32_t width, uint32_t height, TextureColorSpace colorSpace, auto&& pixelFunc) -> TextureAssetData
			{
				GGLAB_ASSERT(width > 0 && height > 0);
				constexpr size_t formatBytes = 4;
				std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * formatBytes);

				for (uint32_t y = 0; y < height; ++y)
				{
					for (uint32_t x = 0; x < width; ++x)
					{
						const auto color = pixelFunc(x, y);
						uint8_t* pixel = pixels.data() + (static_cast<size_t>(y) * width + x) * formatBytes;
						pixel[0] = color[0];
						pixel[1] = color[1];
						pixel[2] = color[2];
						pixel[3] = color[3];
					}
				}
				return TextureLoader::MakeTexture2DRgba8(width, height, pixels, colorSpace);
			};

		const auto makeUploadData = [&, this](ReservedTextureIDIndex idIndex,
			const char* texName,
			TextureSemantic semantic,
			TextureAssetData&& textureData)
			{
				const auto& id = ToTextureId(idIndex);
				CreateTextureEntry(id, texName);

				return MakeTextureUploadData(id, std::move(textureData), semantic);
			};

		const auto makeGeneratedUploadData = [&](ReservedTextureIDIndex idIndex,
			const char* texName,
			TextureSemantic semantic,
			uint32_t width,
			uint32_t height,
			auto&& pixelFunc)
			{
				return makeUploadData(
					idIndex,
					texName,
					semantic,
					makeTextureData(width, height, GetTextureColorSpaceFromSemantic(semantic), pixelFunc));
			};

		std::vector<TextureUploadData> uploads;
		uploads.reserve(utils::ToIndex(ReservedTextureIDIndex::Count));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::BaseColorWhite,
			"BaseColorWhite",
			TextureSemantic::BaseColor,
			1,
			1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
			{
				return { 255, 255, 255, 255 };
			}));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::MissingTextureChecker,
			"MissingTextureChecker",
			TextureSemantic::BaseColor,
			64,
			64,
			[](uint32_t x, uint32_t y) -> std::array<uint8_t, 4>
			{
				const uint32_t tile = 8;
				const bool isPurple = ((x / tile) + (y / tile)) & 1;
				return isPurple ?
					std::array<uint8_t, 4>{ 255, 0, 255, 255 } :
					std::array<uint8_t, 4>{ 0, 0, 0, 255 };
			}));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::NormalFlat,
			"NormalFlat",
			TextureSemantic::Normal,
			1,
			1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
			{
				return { 128, 128, 255, 255 };
			}));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::DefaultMetallicRoughness,
			"DefaultMetallicRoughness",
			TextureSemantic::MetallicRoughness,
			1,
			1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
			{
				// The shader multiplies sampled values by material factors, so an
				// absent metallic-roughness texture must use the multiplicative identity.
				return { 0, 255, 255, 255 };
			}));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::OcclusionWhite,
			"OcclusionWhite",
			TextureSemantic::Occlusion,
			1,
			1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
			{
				return { 255, 255, 255, 255 };
			}));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::EmissiveBlack,
			"EmissiveBlack",
			TextureSemantic::Emissive,
			1,
			1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
			{
				return { 0, 0, 0, 255 };
			}));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::ErrorRed,
			"ErrorRed",
			TextureSemantic::BaseColor,
			1,
			1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
			{
				return { 255, 0, 0, 255 };
			}));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::UVTest,
			"UVTest",
			TextureSemantic::UVTest,
			256,
			256,
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
			}));

		{
			auto texPath = utils::Canonical(std::filesystem::path("Assets/Textures/UVTest1K.png"));
			const auto id = ToTextureId(ReservedTextureIDIndex::UVTestTexture1K);
			CreateTextureEntry(id, "UVTestTexture1K", texPath);
			uploads.emplace_back(MakeTextureUploadData(id, texPath, TextureSemantic::UVTest));
			m_TextureContainer.m_CacheKeyIDMap[
				{ texPath, MakeTextureImportSettings(TextureSemantic::UVTest) }] = id;
		}

		{
			auto texPath = utils::Canonical(std::filesystem::path("Assets/Textures/UVTest4K.png"));
			const auto id = ToTextureId(ReservedTextureIDIndex::UVTestTexture4K);
			CreateTextureEntry(id, "UVTestTexture4K", texPath);
			uploads.emplace_back(MakeTextureUploadData(id, texPath, TextureSemantic::UVTest));
			m_TextureContainer.m_CacheKeyIDMap[
				{ texPath, MakeTextureImportSettings(TextureSemantic::UVTest) }] = id;
		}

		if (!uploads.empty())
		{
			auto batch = m_TransferManager->BeginBatch();
			bool uploadsSucceeded = true;
			for (const auto& data : uploads)
			{
				uploadsSucceeded &= UploadTexture(data, batch);
			}
			GGLAB_UNUSED(batch.Submit(true));
			GGLAB_ASSERT_MSG(uploadsSucceeded, "TextureRegistry failed to initialize reserved textures.");
		}
	}

	void TextureRegistry::Finalize(const RHIFencePoint& fencePoint) noexcept
	{
		for (const auto& texture : m_TextureContainer.m_TextureIDMap | std::views::values)
		{
			if (texture->m_Texture.IsValid())
			{
				m_Device->RecordTextureUse(texture->m_Texture, fencePoint);
				m_Device->DestroyTexture(texture->m_Texture);
				texture->m_Texture.Reset();
			}
			texture->m_Srv.Reset();
		}
	}

	TextureID TextureRegistry::LoadTexture(const std::filesystem::path& path, TextureSemantic semantic) noexcept
	{
		if (path.empty())
		{
			GGLAB_LOG_GRAPHICS_WARN("TextureRegistry::LoadTexture received an empty path.");
			return InvalidTextureID;
		}

		const auto canonicalPath = utils::Canonical(path);
		std::error_code errorCode;
		if (!std::filesystem::exists(canonicalPath, errorCode) ||
			!std::filesystem::is_regular_file(canonicalPath, errorCode))
		{
			GGLAB_LOG_GRAPHICS_WARN("TextureRegistry::LoadTexture received a missing texture file: '{}'.",
				canonicalPath.string());
			return InvalidTextureID;
		}

		const TextureImportSettings importSettings = MakeTextureImportSettings(semantic);
		auto textureId = FindTexture(canonicalPath, importSettings);
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
				return textureId;
			}

			// Do not let a stale, incomplete dynamic entry poison future retries.
			if (IsReservedTextureId(textureId) || !RemoveTexture(textureId))
			{
				return InvalidTextureID;
			}
		}

		auto texUploadData = MakeTextureUploadData(InvalidTextureID, canonicalPath, semantic);
		if (!texUploadData.m_TextureData.IsValid())
		{
			return InvalidTextureID;
		}

		textureId = CreateTexture(canonicalPath, importSettings);
		if (!textureId.IsValid())
		{
			return InvalidTextureID;
		}
		texUploadData.m_TextureId = textureId;
		auto batch = m_TransferManager->BeginBatch();
		const bool uploadSucceeded = UploadTexture(texUploadData, batch);
		GGLAB_UNUSED(batch.Submit(true));
		if (!uploadSucceeded)
		{
			RemoveTexture(textureId);
			return InvalidTextureID;
		}

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

	const RHITextureDesc* TextureRegistry::GetTextureDesc(TextureID textureId) const noexcept
	{
		const auto* texture = GetTexture(textureId);
		return texture && texture->m_IsUploaded ? &texture->m_Desc : nullptr;
	}

	RHIDescriptorHandle TextureRegistry::GetSrvDescriptor(TextureID textureId) const noexcept
	{
		const auto* texture = GetTexture(textureId);
		if (!texture || !texture->m_IsUploaded || !texture->m_Srv.IsValid())
		{
			return {};
		}

		return m_Device->GetTextureViewDescriptor(texture->m_Srv);
	}

	uint32_t TextureRegistry::GetShaderVisibleSrvIndex(TextureID textureId) const noexcept
	{
		const RHIDescriptorHandle descriptor = GetSrvDescriptor(textureId);
		GGLAB_ASSERT_MSG(
			descriptor.IsValid() && descriptor.m_HeapType == RHIDescriptorHeapType::CbvSrvUav,
			"TextureRegistry::GetShaderVisibleSrvIndex: texture SRV descriptor is invalid.");
		return descriptor.m_Index;
	}

	uint32_t TextureRegistry::ResolveSrvIndex(TextureID textureId, ReservedTextureIDIndex fallback) const noexcept
	{
		const auto resolveSrvIndex = [this](RHITextureViewHandle srv) noexcept -> uint32_t
			{
				const RHIDescriptorHandle descriptor = m_Device->GetTextureViewDescriptor(srv);
				GGLAB_ASSERT_MSG(
					descriptor.IsValid() && descriptor.m_HeapType == RHIDescriptorHeapType::CbvSrvUav,
					"TextureRegistry::ResolveSrvIndex: texture SRV descriptor is invalid.");
				return descriptor.m_Index;
			};

		if (const auto* texture = GetTexture(textureId); texture != nullptr)
		{
			if (texture->m_IsUploaded && texture->m_Srv.IsValid())
			{
				return resolveSrvIndex(texture->m_Srv);
			}
		}

		const auto fallbackId = ToTextureId(fallback);
		const auto* fallbackTexture = GetTexture(fallbackId);

		GGLAB_ASSERT(fallbackTexture != nullptr && fallbackTexture->m_Srv.IsValid());

		return resolveSrvIndex(fallbackTexture->m_Srv);
	}

	TextureID TextureRegistry::CreateTexture(const std::filesystem::path& canonicalPath,
		const TextureImportSettings& importSettings) noexcept
	{
		const auto textureId = m_TextureIdCounter.Acquire();
		if (!textureId.IsValid())
		{
			return InvalidTextureID;
		}
		auto pathIdPair = m_TextureContainer.m_CacheKeyIDMap.emplace(
			TextureCacheKey{ canonicalPath, importSettings }, textureId);
		GGLAB_ASSERT_MSG(pathIdPair.second, "TextureRegistry: emplace path and TextureID pair failed.");
		if (!pathIdPair.second)
		{
			return InvalidTextureID;
		}

		auto idTexPair = m_TextureContainer.m_TextureIDMap.emplace(textureId, std::make_unique<Texture>());
		GGLAB_ASSERT_MSG(idTexPair.second, "TextureRegistry: emplace TextureID and Texture pair failed.");
		if (!idTexPair.second)
		{
			m_TextureContainer.m_CacheKeyIDMap.erase(pathIdPair.first);
			return InvalidTextureID;
		}

		idTexPair.first->second->m_Id = textureId;
		idTexPair.first->second->m_Name = StringID(canonicalPath.generic_string());
		idTexPair.first->second->m_SourcePath = canonicalPath;
		idTexPair.first->second->m_DebugLabel = canonicalPath.filename().generic_string();

		return textureId;
	}

	bool TextureRegistry::RemoveTexture(TextureID textureId) noexcept
	{
		if (!textureId.IsValid() || IsReservedTextureId(textureId))
		{
			return false;
		}

		auto textureIter = m_TextureContainer.m_TextureIDMap.find(textureId);
		bool removed = false;
		if (textureIter != m_TextureContainer.m_TextureIDMap.end())
		{
			if (textureIter->second && textureIter->second->m_Texture.IsValid())
			{
				m_Device->DestroyTexture(textureIter->second->m_Texture);
			}
			m_TextureContainer.m_TextureIDMap.erase(textureIter);
			removed = true;
		}
		const size_t removedPaths = std::erase_if(m_TextureContainer.m_CacheKeyIDMap,
			[textureId](const auto& entry) noexcept
			{
				return entry.second == textureId;
			});
		return removed || removedPaths > 0;
	}

	TextureID TextureRegistry::FindTexture(const std::filesystem::path& canonicalPath,
		const TextureImportSettings& importSettings) const noexcept
	{
		const auto& cacheKeyIds = m_TextureContainer.m_CacheKeyIDMap;
		auto iterator = cacheKeyIds.find(TextureCacheKey{ canonicalPath, importSettings });
		if (iterator != cacheKeyIds.end())
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
		uploadData.m_TextureData = TextureLoader::LoadTextureData(
			canonicalPath,
			MakeTextureImportSettings(semantic));
		return uploadData;
	}

	TextureRegistry::TextureUploadData TextureRegistry::MakeTextureUploadData(TextureID textureId,
		TextureAssetData&& textureData, TextureSemantic semantic) noexcept
	{
		TextureUploadData uploadData{};
		uploadData.m_TextureId = textureId;
		uploadData.m_Semantic = semantic;
		uploadData.m_ColorSpace = GetTextureColorSpaceFromSemantic(semantic);
		uploadData.m_TextureData = std::move(textureData);
		return uploadData;
	}

	bool TextureRegistry::UploadTexture(
		const TextureUploadData& uploadData,
		TransferBatch& transferBatch) noexcept
	{
		auto* texture = GetTexture(uploadData.m_TextureId);
		GGLAB_ASSERT_MSG(texture != nullptr, "TextureRegistry::UploadTexture: invalid TextureID.");
		if (!texture)
		{
			return false;
		}

		const TextureAssetData& textureData = uploadData.m_TextureData;
		if (!textureData.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR("TextureRegistry::UploadTexture received invalid texture asset data.");
			return false;
		}
		if (texture->m_Texture.IsValid() || texture->m_IsUploaded)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"TextureRegistry::UploadTexture only supports initial upload of a texture entry.");
			return false;
		}

		RHITextureDesc textureDesc{};
		textureDesc.m_Dimension = RHITextureDimension::Texture2D;
		textureDesc.m_Format = textureData.m_ResourceFormat;
		textureDesc.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::CopyDest;
		textureDesc.m_Extent = textureData.m_Extent;
		textureDesc.m_ArraySize = textureData.m_ArraySize;
		textureDesc.m_MipLevels = textureData.m_MipLevels;
		textureDesc.m_SampleCount = 1;
		const std::string category = std::format(
			"Texture.{}", TextureSemanticDebugText(uploadData.m_Semantic));
		const std::string source = TextureDebugSourcePath(texture->m_SourcePath);
		const RHIResourceDebugIdentityDesc debugIdentity
		{
			.m_Domain = RHIResourceDebugDomain::Asset,
			.m_Category = category,
			.m_Label = texture->m_DebugLabel,
			.m_Source = source,
			.m_StableId = uploadData.m_TextureId.Value(),
		};

		texture->m_Texture = m_Device->CreateTexture(textureDesc, debugIdentity);
		GGLAB_ASSERT_MSG(texture->m_Texture.IsValid(), "TextureRegistry::UploadTexture: failed to create RHI texture.");
		if (!texture->m_Texture.IsValid())
		{
			return false;
		}
		texture->m_Desc = textureDesc;
		texture->m_Desc.m_DebugName = nullptr;

		const RHITextureUploadData textureUploadData = textureData.MakeUploadData();
		if (!transferBatch.UploadTexture(texture->m_Texture, textureUploadData))
		{
			GGLAB_LOG_GRAPHICS_ERROR("TextureRegistry::UploadTexture failed to record the texture upload.");
			return false;
		}

		const RHITextureBarrier barrier
		{
			.m_Texture = texture->m_Texture,
			.m_Before =
			{
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopyDest,
				.m_Layout = RHILayout::CopyDest,
			},
			.m_After =
			{
				.m_Stages = RHIStage::PixelShader | RHIStage::ComputeShader,
				.m_Access = RHIAccess::ShaderResource,
				.m_Layout = RHILayout::ShaderResource,
			},
		};
		transferBatch.TextureBarrier(std::span{ &barrier, 1 });

		RHITextureViewDesc srvDesc{};
		srvDesc.m_Type = RHITextureViewType::ShaderResource;
		srvDesc.m_Dimension = textureData.m_ArraySize > 1 ?
			RHITextureViewDimension::Texture2DArray :
			RHITextureViewDimension::Texture2D;
		srvDesc.m_Format = textureData.m_ViewFormat;
		srvDesc.m_Subresources.m_BaseMip = 0;
		srvDesc.m_Subresources.m_MipCount = textureData.m_MipLevels;
		srvDesc.m_Subresources.m_BaseArraySlice = 0;
		srvDesc.m_Subresources.m_ArraySliceCount = textureData.m_ArraySize;

		texture->m_Srv = m_Device->CreateTextureView(texture->m_Texture, srvDesc);
		GGLAB_ASSERT_MSG(texture->m_Srv.IsValid(), "TextureRegistry::UploadTexture: failed to create RHI texture SRV.");
		if (!texture->m_Srv.IsValid())
		{
			return false;
		}
		texture->m_Semantic = uploadData.m_Semantic;
		texture->m_IsUploaded = true;
		return true;
	}

	void TextureRegistry::CreateTextureEntry(
		TextureID id,
		std::string_view textureName,
		const std::filesystem::path& sourcePath) noexcept
	{
		if (GetTexture(id) == nullptr)
		{
			auto [iterator, result] = m_TextureContainer.m_TextureIDMap.emplace(id, std::make_unique<Texture>());
			if (result)
			{
				auto& texture = iterator->second;
				texture->m_Id = id;
				texture->m_Name = StringID(textureName);
				texture->m_SourcePath = sourcePath;
				texture->m_DebugLabel = textureName;
				texture->m_IsUploaded = false;
				texture->m_Srv.Reset();
				texture->m_Texture.Reset();
				texture->m_Desc = {};
			}
		}
	}
}
