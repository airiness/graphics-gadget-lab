#pragma once
#include "Graphics/GraphicsTypes.h"
#include "Graphics/SamplerTypes.h"
#include "Graphics/TextureAsset.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/VertexData.h"

#include <array>
#include <limits>
#include <stop_token>

namespace gglab
{
	struct ModelImportSettings
	{
		bool m_EnableAnisotropicFiltering = true;
		uint32_t m_MaxAnisotropy = 8;
	};

	struct ImportedTexture
	{
		std::filesystem::path m_CanonicalPath;
		TextureImportSettings m_ImportSettings{};
		TextureSemantic m_Semantic = TextureSemantic::GenericColor;
		TextureAssetData m_Data;
	};

	struct ImportedMaterialTextureBinding
	{
		static constexpr uint32_t InvalidTextureIndex = std::numeric_limits<uint32_t>::max();

		uint32_t m_TextureIndex = InvalidTextureIndex;
		SamplerKey m_SamplerKey{};
		uint32_t m_TexCoordIndex = 0;
	};

	struct ImportedMaterial
	{
		std::string m_Name;
		MaterialProperties m_Properties{};
		std::array<ImportedMaterialTextureBinding, static_cast<size_t>(MaterialTextureSlot::Count)> m_TextureBindings{};
	};

	struct ImportedMesh
	{
		std::string m_Name;
		std::vector<Vertex> m_Vertices;
		std::vector<uint32_t> m_Indices;
		uint32_t m_MaterialIndex = 0;
		math::Sphere m_Sphere{};
		math::Aabb m_Aabb{};
		bool m_HasBounds = false;
	};

	struct ImportedModelMesh
	{
		uint32_t m_MeshIndex = 0;
		uint32_t m_MaterialIndex = 0;
		Matrix m_LocalTransform = Matrix::Identity;
	};

	struct ImportedModel
	{
		std::filesystem::path m_CanonicalPath;
		std::string m_Name;
		ModelType m_Type = ModelType::Invalid;
		std::vector<ImportedTexture> m_Textures;
		std::vector<ImportedMaterial> m_Materials;
		std::vector<ImportedMesh> m_Meshes;
		std::vector<ImportedModelMesh> m_MeshInstances;
	};

	struct ModelImportResult
	{
		ImportedModel m_Model;
		std::string m_Error;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return m_Error.empty() && !m_Model.m_Meshes.empty();
		}
	};

	// CPU-only model import. This class never touches asset registries, RHI
	// objects, GPU handles, or session-owned state, so it is safe to run on
	// TaskSystem workers.
	class ModelImporter
	{
	public:
		[[nodiscard]] static ModelImportResult Import(
			const std::filesystem::path& path,
			const ModelImportSettings& settings,
			std::stop_token stopToken = {},
			const ProgressReporter& progress = {}) noexcept;
	};
}
