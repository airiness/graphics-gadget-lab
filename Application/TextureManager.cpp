#include "Precompiled.h"
#include "TextureManager.h"
#include "DX12Device.h"
#include "Texture.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	TextureManager::TextureManager(DX12Device* dx12Device) noexcept :
		m_DX12Device(dx12Device)
	{
	}

	TextureManager::~TextureManager() noexcept
	{
	}

	Texture* TextureManager::LoadTexture(const std::filesystem::path& texPath) noexcept
	{
		std::lock_guard lock(m_Mutex);

		// Use canonical path as unordered map key.
		auto cannonicalPath = std::filesystem::canonical(texPath);
		auto searchIter = m_Textures.find(cannonicalPath);
		if (searchIter != m_Textures.end())
		{
			return searchIter->second.get();
		}

		const auto extension = texPath.extension().string();

		DirectX::TexMetadata metaData;
		DirectX::ScratchImage scratchImage;

		if (extension == ".dds")
		{
			utility::ThrowIfFailed(LoadFromDDSFile(texPath.c_str(), DirectX::DDS_FLAGS::DDS_FLAGS_FORCE_RGB, &metaData, scratchImage));
		}
		else
		{
			utility::ThrowIfFailed(LoadFromWICFile(texPath.c_str(), DirectX::WIC_FLAGS_FORCE_RGB, &metaData, scratchImage));
		}

		auto pair = m_Textures.emplace(cannonicalPath, std::make_unique<Texture>());
		GGLAB_ASSERT_MSG(pair.second == true, "Create Texture failed.");

		auto& tex = pair.first->second;
		tex->m_ScratchImage = std::move(scratchImage);

		tex->m_ScratchImage.GetMetadata();
		UploadTexture(tex.get());
		return tex.get();
	}

	void TextureManager::UploadTexture(Texture* texture) noexcept
	{
		texture->Upload(m_DX12Device);
	}
}