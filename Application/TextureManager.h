#pragma once
//#include "Texture.h"

namespace graphicsGadgetLab
{
	class DX12Device;
	class Texture;
	class TextureManager final
	{
	public:
		explicit TextureManager(DX12Device* dx12Device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TextureManager);
		~TextureManager() noexcept;

		Texture* LoadTexture(const std::filesystem::path& texPath) noexcept;

	private:
		void UploadTexture(Texture* texture) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;

		using TextureContainer = std::unordered_map<std::filesystem::path, std::unique_ptr<Texture>>;
		TextureContainer m_Textures;

		std::mutex m_Mutex;
	};
}
