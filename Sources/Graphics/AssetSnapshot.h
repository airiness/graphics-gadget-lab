#pragma once
#include "Graphics/GraphicsTypes.h"

namespace gglab
{
	class AssetManager;

	struct AssetSnapshot
	{
		struct Model
		{
			ModelID m_Id{};
			std::filesystem::path m_SourcePath;
			ModelType m_Type = ModelType::Invalid;
			StringID m_Name{};
			uint32_t m_MeshInstanceCount = 0;
		};

		struct Texture
		{
			TextureID m_Id{};
			std::filesystem::path m_SourcePath;
			TextureSemantic m_Semantic = TextureSemantic::Unknown;
			StringID m_Name{};
			bool m_IsUploaded = false;
			bool m_IsReserved = false;
		};

		std::vector<Model> m_Models;
		std::vector<Texture> m_Textures;
	};

	[[nodiscard]] AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept;
}
