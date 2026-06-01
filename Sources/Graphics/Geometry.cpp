#include "Core/Precompiled.h"
#include "Graphics/Geometry.h"
#include "Core/Components.h"
#include "Graphics/AssetManager.h"
#include "Graphics/SamplerRegistry.h"
#include "Core/World.h"

namespace gglab
{
	namespace primitive
	{
		entt::entity Cube::Create(const CreateInfo& info) noexcept
		{
			auto* assetManager = info.m_AssetManager;
			auto* samplerRegistry = info.m_SamplerRegistry;
			auto& registry = info.m_World->GetRegistry();

			if (assetManager->GetModel(ProceduralCubeModelID) == nullptr)
			{
				std::unique_ptr<Model> cubeModel = std::make_unique<Model>();
				cubeModel->m_Id = ProceduralCubeModelID;
				cubeModel->m_Type = ModelType::Procedural;
				cubeModel->m_Name = StringID("ProceduralCube");

				if (assetManager->GetMesh(ProceduralCubeMeshID) == nullptr)
				{
					std::unique_ptr<Mesh> cubeMesh = std::make_unique<Mesh>();
					cubeMesh->m_Id = ProceduralCubeMeshID;

					AssetManager::MeshUploadData meshUploadData;
					meshUploadData.m_MeshId = ProceduralCubeMeshID;
					meshUploadData.m_VerticesData = GetVerticesData();
					meshUploadData.m_IndicesData = GetIndicesData();

					if (assetManager->GetMaterial(ProceduralCubeMaterialID) == nullptr)
					{
						std::unique_ptr<Material> cubeMaterial = std::make_unique<Material>();
						cubeMaterial->m_Id = ProceduralCubeMaterialID;
						cubeMaterial->m_BaseColorBinding.m_TextureId = ToTextureId(ReservedTextureIDIndex::UVTestTexture1K);
						cubeMaterial->m_BaseColorBinding.m_SamplerId = samplerRegistry->GetPresetSamplerId(SamplerPreset::AnisotropicClamp);
						assetManager->AddMaterial(std::move(cubeMaterial));
					};

					assetManager->AddMesh(std::move(cubeMesh), meshUploadData);
				}

				ModelMesh modelMesh{};
				modelMesh.m_MeshId = ProceduralCubeMeshID;
				modelMesh.m_MaterialId = ProceduralCubeMaterialID;

				cubeModel->m_MeshInstance.push_back(modelMesh);

				assetManager->AddModel(std::move(cubeModel));
			}

			entt::entity cubeEntity = registry.create();

			components::TransformComponent transComp{};
			registry.emplace<components::TransformComponent>(cubeEntity, transComp);

			components::ModelComponent modelComp{};
			modelComp.m_ModelId = ProceduralCubeModelID;
			registry.emplace<components::ModelComponent>(cubeEntity, modelComp);

			return cubeEntity;
		}

		std::vector<Vertex> Cube::GetVerticesData() noexcept
		{
			constexpr std::array<std::array<Vector3, VertexCountPerFace>, FaceCount> facePositions =
			{ {
				// Front face (-Z)
				{{{-1.0f, -1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f}}},
				// Back face (+Z)
				{{{ 1.0f, -1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}, {-1.0f, -1.0f,  1.0f}}},
				// Top face (+Y)
				{{{-1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f, -1.0f}}},
				// Bottom face (-Y)
				{{{-1.0f, -1.0f,  1.0f}, {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f,  1.0f}}},
				// Left face (-X)
				{{{-1.0f, -1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}}},
				// Right face (+X)
				{{{ 1.0f, -1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, { 1.0f,  1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f}}},
			} };

			constexpr std::array<Vector2, VertexCountPerFace> texCoords =
			{ {
				{ 0.0f, 1.0f },
				{ 0.0f, 0.0f },
				{ 1.0f, 0.0f },
				{ 1.0f, 1.0f },
			} };

			constexpr std::array<Vector3, FaceCount> faceNormals =
			{ {
				{ 0.0f,  0.0f, -1.0f }, // Front
				{ 0.0f,  0.0f,  1.0f }, // Back
				{ 0.0f,  1.0f,  0.0f }, // Top
				{ 0.0f, -1.0f,  0.0f }, // Bottom
				{-1.0f,  0.0f,  0.0f }, // Left
				{ 1.0f,  0.0f,  0.0f }, // Right
			} };

			std::vector<Vertex> vertices;
			vertices.reserve(FaceCount * VertexCountPerFace);

			for (size_t face = 0; face < FaceCount; ++face)
			{
				const auto& n = faceNormals[face];
				const auto& p = facePositions[face];

				for (size_t i = 0; i < VertexCountPerFace; ++i)
				{
					vertices.push_back({ p[i], n, texCoords[i] });
				}
			}

			return vertices;
		}

		std::vector<uint32_t> Cube::GetIndicesData() noexcept
		{
			std::vector<uint32_t> indices;
			indices.reserve(36); // 6 faces * 2 triangles * 3 indices

			for (uint32_t face = 0; face < FaceCount; ++face)
			{
				uint32_t base = face * VertexCountPerFace;
				indices.push_back(base + 0);
				indices.push_back(base + 1);
				indices.push_back(base + 2);

				indices.push_back(base + 0);
				indices.push_back(base + 2);
				indices.push_back(base + 3);
			}

			return indices;
		}
	}
}