#pragma once
namespace graphicsGadgetLab
{
	class DX12Device;
	class Texture;
	class TextureManager
	{
	public:
		explicit TextureManager(DX12Device* dx12Device);
		~TextureManager();
		GGLAB_DELETE_COPYABLE_MOVABLE(TextureManager);

		Texture* LoadTexture(const std::filesystem::path& texPath) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;

		using TextureContainer = std::unordered_map<uint64_t, std::unique_ptr<Texture>>;
		TextureContainer m_Textures;


	};
}
