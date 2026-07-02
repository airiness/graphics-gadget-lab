#include "Core/Precompiled.h"
#include "Diagnostics/Builders/AssetSnapshotBuilder.h"
#include "Diagnostics/Snapshots/AssetSnapshot.h"
#include "Graphics/AssetManager.h"
#include "Graphics/TextureRegistry.h"

#include <algorithm>

namespace gglab
{
	AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept
	{
		AssetSnapshot snapshot{};

		const auto findModelSourcePath = [&assetManager](ModelID modelId) -> std::filesystem::path
			{
				for (const auto& [path, pathModelId] : assetManager.m_ModelContainer.m_PathIDMap)
				{
					if (pathModelId == modelId)
					{
						return path;
					}
				}
				return {};
			};

		snapshot.m_Models.reserve(assetManager.m_ModelContainer.m_ModelIDMap.size());
		for (const auto& [modelId, model] : assetManager.m_ModelContainer.m_ModelIDMap)
		{
			AssetSnapshot::Model modelSnapshot{};
			modelSnapshot.m_Id = modelId;
			modelSnapshot.m_SourcePath = findModelSourcePath(modelId);
			modelSnapshot.m_Type = model->m_Type;
			modelSnapshot.m_Name = model->m_Name;
			modelSnapshot.m_MeshInstanceCount = static_cast<uint32_t>(model->m_MeshInstance.size());
			snapshot.m_Models.emplace_back(std::move(modelSnapshot));
		}

		std::sort(snapshot.m_Models.begin(), snapshot.m_Models.end(),
			[](const AssetSnapshot::Model& lhs, const AssetSnapshot::Model& rhs)
			{
				return lhs.m_Id.Value() < rhs.m_Id.Value();
			});

		const TextureRegistry* textureRegistry = assetManager.m_TextureRegistry;
		if (textureRegistry)
		{
			const auto findTextureSourcePath = [textureRegistry](TextureID textureId) -> std::filesystem::path
				{
					for (const auto& [path, pathTextureId] : textureRegistry->m_TextureContainer.m_PathIDMap)
					{
						if (pathTextureId == textureId)
						{
							return path;
						}
					}
					return {};
				};

			snapshot.m_Textures.reserve(textureRegistry->m_TextureContainer.m_TextureIDMap.size());
			for (const auto& [textureId, texture] : textureRegistry->m_TextureContainer.m_TextureIDMap)
			{
				AssetSnapshot::Texture textureSnapshot{};
				textureSnapshot.m_Id = textureId;
				textureSnapshot.m_SourcePath = findTextureSourcePath(textureId);
				textureSnapshot.m_Semantic = texture->m_Semantic;
				textureSnapshot.m_Name = texture->m_Name;
				textureSnapshot.m_IsUploaded = texture->m_IsUploaded;
				textureSnapshot.m_IsReserved = IsReservedTextureId(textureId);
				snapshot.m_Textures.emplace_back(std::move(textureSnapshot));
			}

			std::sort(snapshot.m_Textures.begin(), snapshot.m_Textures.end(),
				[](const AssetSnapshot::Texture& lhs, const AssetSnapshot::Texture& rhs)
				{
					return lhs.m_Id.Value() < rhs.m_Id.Value();
				});
		}

		return snapshot;
	}
}
