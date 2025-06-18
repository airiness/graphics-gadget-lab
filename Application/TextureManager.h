#pragma once
#include "DX12Texture.h"
#include "DX12Descriptor.h"

#include <DirectXTex.h>

namespace graphicsGadgetLab
{
	class DX12Device;
	
	struct Texture
	{
		DX12Texture m_DX12Texture;
		DX12Descriptor m_Descriptor;

		
	};

	class TextureManager
	{
	public:
		explicit TextureManager(DX12Device* dx12Device) noexcept;
		~TextureManager() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TextureManager);

		Texture* LoadTexture(const std::filesystem::path& texPath) noexcept;

	private:
		void UploadTexture(Texture* texture) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;

		using TextureContainer = std::unordered_map<uint64_t, std::unique_ptr<Texture>>;
		TextureContainer m_Textures;


	};
}
