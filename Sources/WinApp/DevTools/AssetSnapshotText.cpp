#include "DevTools/AssetSnapshotText.h"
#include "Core/StringIdFormatting.h"

namespace gglab::devtools
{
	std::string ModelDisplayName(const AssetSnapshot::Model& model)
	{
		if (!model.m_SourcePath.empty())
		{
			return model.m_SourcePath.filename().generic_string();
		}

		const std::string name = utils::StringIdToString(model.m_Name);
		return name.empty() ? std::format("Model {}", model.m_Id.Value()) : name;
	}

	std::string TextureDisplayName(const AssetSnapshot::Texture& texture)
	{
		if (!texture.m_SourcePath.empty())
		{
			return texture.m_SourcePath.filename().generic_string();
		}

		const std::string name = utils::StringIdToString(texture.m_Name);
		return name.empty() ? std::format("Texture {}", texture.m_Id.Value()) : name;
	}
}
