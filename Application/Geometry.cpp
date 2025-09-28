#include "Precompiled.h"
#include "Geometry.h"
#include "Application.h"
#include "Components.h"
#include "AssetManager.h"

namespace gglab
{
	namespace primitive
	{
		const char* const Cube::TextPath = "Assets/textures/UVChecker1K.png";
		entt::entity Cube::Create() noexcept
		{
			auto assetManager = Application::GetInstance()->GetAssetManager();
			auto& enttRegistry = Application::GetInstance()->GetEnttRegistry();

			entt::entity cubeEntity = enttRegistry.create();

			Transform transform;
			enttRegistry.emplace<Transform>(cubeEntity, transform);

			Model cubeModel;
			cubeModel.m_Type = ModelType::ModelType_Procedual;

			if (assetManager->GetMesh(PocedualCubeMeshID) == nullptr)
			{
				std::unique_ptr<Mesh> cubeMesh = std::make_unique<Mesh>();
				cubeMesh->m_MeshId = PocedualCubeMeshID;

				AssetManager::MeshUploadData meshUploadData;
				meshUploadData.m_MeshId = PocedualCubeMeshID;
				meshUploadData.m_VerticesData = GetVerticesData();
				meshUploadData.m_IndicesData = GetIndicesData();

				if (assetManager->GetMaterial(PocedualCubeMaterialID) == nullptr)
				{
					std::unique_ptr<Material> cubeMaterial = std::make_unique<Material>();
					cubeMaterial->m_MaterialId = PocedualCubeMaterialID;
					cubeMaterial->m_BaseColorTex = assetManager->GetTextureID(TextPath);
					assetManager->AddMaterial(std::move(cubeMaterial));
				};

				cubeMesh->m_MaterialId = PocedualCubeMaterialID;
				assetManager->AddMesh(std::move(cubeMesh), meshUploadData);
			}

			cubeModel.m_Meshes.push_back(PocedualCubeMeshID);

			enttRegistry.emplace<Model>(cubeEntity, std::move(cubeModel));

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