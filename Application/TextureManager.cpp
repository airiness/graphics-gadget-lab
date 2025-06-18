#include "Precompiled.h"
#include "TextureManager.h"
#include "DX12Device.h"
#include "DX12Texture.h"
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
		// Use canonical path as unordered map key.
		auto cannonicalPath = std::filesystem::canonical(texPath);

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

		auto texture = std::make_unique<Texture>();
		texture->;

		auto dx12Texture = std::make_unique<DX12Texture>(m_DX12Device,
			D3D12_HEAP_TYPE_DEFAULT,
			CD3DX12_RESOURCE_DESC::Tex2D(metaData.format,
				static_cast<UINT64>(metaData.width),
				static_cast<UINT>(metaData.height),
				static_cast<UINT16>(metaData.arraySize)),
			D3D12_RESOURCE_STATE_COMMON);

		


		return nullptr;
	}
	void TextureManager::UploadTexture(Texture* texture) noexcept
	{
	}
}