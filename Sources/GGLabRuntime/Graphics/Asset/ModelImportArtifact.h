#pragma once
#include "Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/AssetContentFingerprint.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "Graphics/Asset/Loading/ModelImporter.h"
#include "Graphics/Asset/TextureArtifact.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	class TextureArtifactCache;

	struct ModelImportTexture
	{
		std::filesystem::path m_CanonicalPath;
		TextureImportSettings m_ImportSettings{};
		TextureSemantic m_Semantic = TextureSemantic::GenericColor;
		TextureArtifactHandle m_Artifact;
		AssetContentFingerprint m_ContentFingerprint{};
		SourceDigest m_SourceDigest{};
		DerivedDataKey m_DerivedDataKey{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_ImportSettings.m_Semantic == m_Semantic && m_Artifact &&
				m_Artifact->IsValid() && m_ContentFingerprint.IsValid() &&
				m_SourceDigest.IsValid() && m_DerivedDataKey.IsValid();
		}
	};

	struct ResolvedModelImportTexture
	{
		TextureArtifactHandle m_Artifact;
		AssetContentFingerprint m_ContentFingerprint{};
		SourceDigest m_SourceDigest{};
		DerivedDataKey m_DerivedDataKey{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Artifact && m_Artifact->IsValid() && m_ContentFingerprint.IsValid() &&
				m_SourceDigest.IsValid() && m_DerivedDataKey.IsValid();
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

	struct ModelMeshUploadSource
	{
		ModelImportArtifactHandle m_Owner;
		uint32_t m_MeshIndex = 0;

		[[nodiscard]] const ImportedMesh* GetMesh() const noexcept
		{
			return m_Owner && m_MeshIndex < m_Owner->m_Meshes.size()
				? &m_Owner->m_Meshes[m_MeshIndex]
				: nullptr;
		}

		[[nodiscard]] std::span<const Vertex> GetVertices() const noexcept
		{
			const ImportedMesh* mesh = GetMesh();
			return mesh ? std::span<const Vertex>(mesh->m_Vertices) : std::span<const Vertex>{};
		}

		[[nodiscard]] std::span<const uint32_t> GetIndices() const noexcept
		{
			const ImportedMesh* mesh = GetMesh();
			return mesh ? std::span<const uint32_t>(mesh->m_Indices) : std::span<const uint32_t>{};
		}

		[[nodiscard]] bool IsValid() const noexcept { return GetMesh() != nullptr; }

		void Reset() noexcept { *this = {}; }
	};

	[[nodiscard]] ModelImportArtifactHandle CreateModelImportArtifact(ImportedModel&& importedModel,
		std::vector<ResolvedModelImportTexture>&& resolvedTextures,
		TextureArtifactCache& textureArtifactCache) noexcept;
}
