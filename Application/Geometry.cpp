#include "Precompiled.h"
#include "Geometry.h"
#include "Application.h"
#include "DX12Device.h"
#include "DX12Buffer.h"
#include "Components.h"
#include "AssetManager.h"

namespace graphicsGadgetLab
{
	namespace primitiveGeometry
	{
		entt::entity Cube::Create() noexcept
		{
			auto assetManager = Application::GetInstance()->GetAssetManager();
			auto& enttRegistry = Application::GetInstance()->GetEnttRegistry();

			entt::entity cubeEntity = enttRegistry.create();

			Transform transform;
			enttRegistry.emplace<Transform>(cubeEntity, transform);

			Mesh mesh;
			mesh.m_MeshID = IndividualMeshID;

			AssetManager::MeshUploadData meshUploadData;
			meshUploadData.m_VerticesData = GetVerticesData();
			meshUploadData.m_IndicesData = GetIndicesData();

			auto meshTuple = std::make_tuple(&mesh, meshUploadData);
			std::vector<std::tuple<Mesh*, const AssetManager::MeshUploadData&>> meshTuples;
			meshTuples.push_back(meshTuple);
			assetManager->UploadMeshes(meshTuples);

			enttRegistry.emplace<Mesh>(cubeEntity, std::move(mesh));

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