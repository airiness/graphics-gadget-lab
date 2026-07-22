#pragma once
#include "Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/Loading/ModelImporter.h"
#include "Graphics/Asset/TextureArtifact.h"

#include <algorithm>
#include <memory>

namespace gglab
{
	class TextureArtifactCache;

	struct ModelImportTexture
	{
		std::filesystem::path m_CanonicalPath;
		TextureImportSettings m_ImportSettings{};
		TextureSemantic m_Semantic = TextureSemantic::GenericColor;
		TextureArtifactHandle m_Artifact;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_ImportSettings.m_Semantic == m_Semantic &&
				m_Artifact && m_Artifact->IsValid();
		}
	};

	struct ModelImportArtifact
	{
		std::filesystem::path m_CanonicalPath;
		std::string m_Name;
		ModelType m_Type = ModelType::Invalid;
		std::vector<ModelImportTexture> m_Textures;
		std::vector<ImportedMaterial> m_Materials;
		std::vector<ImportedMesh> m_Meshes;
		std::vector<ImportedModelMesh> m_MeshInstances;
		ArtifactContentDigest m_ContentDigest{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return !m_Meshes.empty() && m_ContentDigest.IsValid() &&
				std::ranges::all_of(m_Textures, &ModelImportTexture::IsValid);
		}

		[[nodiscard]] uint64_t GetAllocatedBytes() const noexcept;
	};

	using ModelImportArtifactHandle = std::shared_ptr<const ModelImportArtifact>;

	[[nodiscard]] ModelImportArtifactHandle CreateModelImportArtifact(
		ImportedModel&& importedModel,
		TextureArtifactCache& textureArtifactCache) noexcept;
}
