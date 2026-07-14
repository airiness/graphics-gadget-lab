#pragma once
#include "Graphics/Asset/ReservedTexture.h"
#include "Graphics/TextureAsset.h"

#include <string_view>
#include <vector>

namespace gglab
{
	struct BuiltinTextureAsset
	{
		ReservedTextureIDIndex m_Id = ReservedTextureIDIndex::BaseColorWhite;
		std::string_view m_Name;
		TextureSemantic m_Semantic = TextureSemantic::GenericColor;
		TextureAssetData m_Data;
	};

	class BuiltinTextureFactory
	{
	public:
		[[nodiscard]] static std::vector<BuiltinTextureAsset> BuildBootstrapTextures() noexcept;
	};
}
