#include "Precompiled.h"
#include "TextureManager.h"
#include "DX12Device.h"

#include <DirectXTex.h>

namespace graphicsGadgetLab
{
	TextureManager::TextureManager(DX12Device* dx12Device) :
		m_DX12Device(dx12Device)
	{
	}

	TextureManager::~TextureManager()
	{
	}

	Texture* TextureManager::LoadTexture(const std::filesystem::path& texPath) noexcept
	{
		return nullptr;
	}
}